#ifdef HAVE_HDF5

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include "FileIO/hdf5_reader.h"
#include "FileIO/file_layout.h"
#include "FileIO/file_mask.h"
#include "data/header_info.h"
#include "core/physics_constants.h"

namespace {
  using Hdf5ProfileClock = std::chrono::steady_clock;

  double elapsed_ms(Hdf5ProfileClock::time_point start)
  {
    return std::chrono::duration<double, std::milli>(
             Hdf5ProfileClock::now() - start)
      .count();
  }

  std::string partPath(int ptype, const std::string& dsName) {
    return "/PartType" + std::to_string(ptype) + "/" + dsName;
  }

  struct Hdf5FieldReadStat {
    std::string fieldName;
    std::string sourceName;
    size_t chunks = 0;
    size_t elements = 0;
    size_t bytes = 0;
    double readMs = 0.0;
    double dispatchMs = 0.0;
    double tempSynthInputMs = 0.0;
  };

  const char* yes_no(bool value)
  {
    return value ? "yes" : "no";
  }

  float fast_cbrt_positive_float(float x)
  {
    if (!(x > 0.0f)) return 0.0f;
    if (!std::isfinite(x)) return x;

    uint32_t bits = 0;
    std::memcpy(&bits, &x, sizeof(bits));
    bits = bits / 3u + 709921077u;

    float y = 0.0f;
    std::memcpy(&y, &bits, sizeof(y));
    y = (2.0f * y + x / (y * y)) * (1.0f / 3.0f);
    y = (2.0f * y + x / (y * y)) * (1.0f / 3.0f);
    return y;
  }

  size_t inferCountFromDataset(H5::H5File& f, int ptype, const std::string& dsName) {
    try {
      H5::DataSet ds = f.openDataSet(partPath(ptype, dsName));
      H5::DataSpace sp = ds.getSpace();
      int nd = sp.getSimpleExtentNdims();
      if (nd < 1) return 0;
      std::vector<hsize_t> dims((size_t)nd, 0);
      sp.getSimpleExtentDims(dims.data());
      return (size_t)dims[0];
    } catch (...) {
      return 0;
    }
  }

  H5::PredType h5_memtype_from_source(DataType t)
  {
    switch(t){
    case DataType::Float:  return H5::PredType::NATIVE_FLOAT;
    case DataType::Double: return H5::PredType::NATIVE_DOUBLE;
    case DataType::Int32:  return H5::PredType::NATIVE_INT32;
    case DataType::Int64:  return H5::PredType::NATIVE_INT64;   // or NATIVE_LLONG
    }
    // fallback
    return H5::PredType::NATIVE_FLOAT;
  }

  
  DataType mapMetaToDataType(const HDF5Utils::H5DatasetMeta& m)
  {
    if (m.cls == H5T_FLOAT) {
      if (m.bytes == 4) return DataType::Float;
      if (m.bytes == 8) return DataType::Double;
    } else if (m.cls == H5T_INTEGER) {
      if (m.bytes == 4) return DataType::Int32;
      if (m.bytes == 8) return DataType::Int64;
    }
    throw std::runtime_error("unsupported dataset dtype");
  }

  int mapMetaToComps(const HDF5Utils::H5DatasetMeta& m)
  {
    if (m.rank == 1) return 1;
    if (m.rank == 2) return (int)m.dims[1];
    throw std::runtime_error("unsupported dataset rank");
  }

  void resetStoreFromSpec(FieldLayout& fl)
  {
    fl.store = nullptr;
    if (fl.dest != DestKind::AoSCore) return;

    const bool isF32 = (fl.spec.type == DataType::Float);
    const bool isF64 = (fl.spec.type == DataType::Double);
    const bool isI32 = (fl.spec.type == DataType::Int32);
    const bool isI64 = (fl.spec.type == DataType::Int64);

    switch (fl.ftype) {
    case FieldKey::Position:    fl.store = isF32 ? &store_pos_f32 : (isF64 ? &store_pos_f64 : nullptr); break;
    case FieldKey::Velocity:    fl.store = isF32 ? &store_vel_f32 : (isF64 ? &store_vel_f64 : nullptr); break;
    case FieldKey::Mass:        fl.store = isF32 ? &store_mass_f32 : (isF64 ? &store_mass_f64 : nullptr); break;
    case FieldKey::Density:     fl.store = isF32 ? &store_density_f32 : (isF64 ? &store_density_f64 : nullptr); break;
    case FieldKey::Temperature: fl.store = isF32 ? &store_temp_f32 : (isF64 ? &store_temp_f64 : nullptr); break;
    case FieldKey::Hsml:        fl.store = isF32 ? &store_hsml_f32 : (isF64 ? &store_hsml_f64 : nullptr); break;
    case FieldKey::Volume:      fl.store = isF32 ? &store_volume_f32 : (isF64 ? &store_volume_f64 : nullptr); break;
    case FieldKey::Type:        fl.store = isI32 ? &store_type_i32 : (isI64 ? &store_type_i64 : nullptr); break;
    default: break;
    }
  }

  void update_layout_from_hdf5(FieldLayout& fl, const H5::DataSet& ds)
  {
    const HDF5Utils::H5DatasetMeta m = HDF5Utils::getDatasetMeta(ds);
    fl.spec.type  = mapMetaToDataType(m);
    fl.spec.count = mapMetaToComps(m);
    resetStoreFromSpec(fl);
  }

  void copy_chunk_to_soa(SimulationBlock& out,
			 const FieldLayout& fl,
			 size_t outBase,
			 size_t nread,
			 size_t bpp,
			 const std::vector<uint8_t>& buf,
			 bool masked,
			 const std::vector<uint32_t>* keepPtr)
  {
    auto& f = out.soa[fl.soaKey];

    if (!masked) {
      std::memcpy(f.bytes.data() + outBase * bpp, buf.data(), nread * bpp);
      return;
    }

    uint8_t* dst0 = f.bytes.data() + outBase * bpp;
    for (size_t kk = 0; kk < keepPtr->size(); ++kk) {
      const uint32_t j = (*keepPtr)[kk];
      std::memcpy(dst0 + kk * bpp,
		  buf.data() + (size_t)j * bpp,
		  bpp);
    }
  }
} // namespace

H5::DataSet HDF5Reader::openDataSetWithDAPL(const std::string& fullPath) const{
  hid_t did = H5Dopen2(file_.getId(), fullPath.c_str(), dapl_);
  if (did < 0) throw H5::DataSetIException("H5Dopen2", "open failed");
  return H5::DataSet(did);
}

bool HDF5Reader::readRange(SimulationBlock& out,
			   size_t begin, size_t count,
			   const std::vector<FieldSpec>& fields,
			   ParticleMask* mask)
{
  const auto totalStart = Hdf5ProfileClock::now();
  if (begin + count > npart_) return false;

  BinaryReadLayout layout = buildBinaryReadLayout(fields, true);
  const bool masked = (mask != nullptr) && mask->active();

  double maskMs = 0.0;
  size_t totalKept = count;
  if (masked) {
    const auto t0 = Hdf5ProfileClock::now();
    if (!prepareMaskedOutput_(begin, count, *mask, totalKept))
      return false;
    maskMs = elapsed_ms(t0);
  }

  const size_t globalBegin = begin;
  const size_t globalEnd   = begin + count;

  const auto layoutStart = Hdf5ProfileClock::now();
  if (!finalize_layout_from_hdf5_(layout))
    return false;
  const double layoutMs = elapsed_ms(layoutStart);

  const auto allocStart = Hdf5ProfileClock::now();
  allocate_output_from_layout_(out, layout, totalKept);
  double allocMs = elapsed_ms(allocStart);

  const TempSynthRequest tempReq = build_temp_synth_request_(layout);

  size_t outWriteCursor = 0;
  double totalProbeMs = 0.0;
  double totalOpenDatasetMs = 0.0;
  double totalReadMs = 0.0;
  double totalDispatchMs = 0.0;
  double totalTempInputMs = 0.0;
  double totalTempSynthMs = 0.0;
  size_t totalChunks = 0;
  size_t totalReadBytes = 0;

  std::fprintf(stderr,
               "[HDF5] readRange begin=%zu requested=%zu masked=%s kept=%zu\n",
               begin,
               count,
               yes_no(masked),
               totalKept);

  for (int ptype = 0; ptype < 6; ++ptype) {
    if (flag_skip_DM_ && ptype == 1) continue;
    if (masked && mask && !mask->typeEnabled(ptype)) {
      std::fprintf(stderr,
                   "[HDF5] ptype=%d source=%zu count=%zu written=0 fields=0 chunks=0 skippedByMask=yes\n",
                   ptype,
                   count_[ptype],
                   count_[ptype]);
      continue;
    }

    const size_t pBeg = IndexStart_[ptype];
    const size_t pEnd = IndexStart_[ptype + 1];
    if (pEnd <= globalBegin || globalEnd <= pBeg) continue;

    const size_t subBegG  = std::max(globalBegin, pBeg);
    const size_t subEndG  = std::min(globalEnd,   pEnd);
    const size_t subCount = subEndG - subBegG;

    const size_t localStart = subBegG - pBeg;
    const size_t outStart   = subBegG - globalBegin;

    size_t done = 0;
    std::vector<uint32_t> keep;

    const auto openStart = Hdf5ProfileClock::now();
    std::vector<OpenedField> opened;
    if (!build_opened_fields_for_ptype_(ptype, layout, opened))
      return false;
    for (const OpenedField& of : opened) {
      if (of.fl) {
        out.markLoadedFieldForType(GetFieldKeyDisplayName(of.fl->ftype), ptype);
      }
    }
    const double openMs = elapsed_ms(openStart);
    totalOpenDatasetMs += openMs;

    const auto probeStart = Hdf5ProfileClock::now();
    const TempSynthAvailability tempAvail =
      probe_temp_synth_availability_(ptype, opened, tempReq);
    const double probeMs = elapsed_ms(probeStart);
    totalProbeMs += probeMs;

    const bool needSynth = tempAvail.needSynth;

    std::vector<Hdf5FieldReadStat> fieldStats(opened.size());
    for (size_t i = 0; i < opened.size(); ++i) {
      fieldStats[i].fieldName = GetFieldKeyDisplayName(opened[i].fl->ftype);
      fieldStats[i].sourceName = opened[i].fl->spec.sourceName;
    }

    double ptypePrepareMs = 0.0;
    double ptypeTempSynthMs = 0.0;
    size_t ptypeChunks = 0;
    size_t ptypeWritten = 0;
    
    while (done < subCount) {      
      const size_t n = std::min(blockSize_, subCount - done);
      
      ChunkContext ctx;
      ctx.ptype      = ptype;
      ctx.localStart = localStart;
      ctx.done       = done;
      ctx.nread      = n;
      ctx.outStart   = outStart;
      ctx.masked     = masked;

      const auto prepareStart = Hdf5ProfileClock::now();
      if (!prepare_chunk_context_(ctx, mask, keep, outWriteCursor))
	return false;
      ptypePrepareMs += elapsed_ms(prepareStart);

      if (ctx.nwrite == 0) {
	done += n;
	continue;
      }
      ++ptypeChunks;
      ++totalChunks;
      ptypeWritten += ctx.nwrite;

      initialize_particle_defaults_chunk_(out, ptype, ctx.outBase, ctx.nwrite);

      TempSynthBuffers tmp;
      if (needSynth)
	init_temp_synth_buffers_(tmp, tempAvail, ctx.nwrite);
      
      for (size_t fieldIndex = 0; fieldIndex < opened.size(); ++fieldIndex) {
        auto& of = opened[fieldIndex];
        Hdf5FieldReadStat& stat = fieldStats[fieldIndex];
        of.buf.resize(n * of.bpp);
        const H5::PredType memType = h5_memtype_from_source(of.fl->spec.type);

        const auto readStart = Hdf5ProfileClock::now();
        HDF5Utils::readHyperslabBytes(of.ds, memType,
				      (hsize_t)(ctx.localStart + ctx.done),
				      (hsize_t)ctx.nread,
				      of.comps, of.buf, of.elemSz);
	const double readMs = elapsed_ms(readStart);
        const size_t readBytes = ctx.nread * of.bpp;
        stat.readMs += readMs;
        stat.chunks += 1;
        stat.elements += ctx.nread;
        stat.bytes += readBytes;
        totalReadMs += readMs;
        totalReadBytes += readBytes;

        const auto dispatchStart = Hdf5ProfileClock::now();
        bool dispatchedWithTempSynth = false;
        if (needSynth) {
          dispatchedWithTempSynth =
            dispatch_h2_and_temperature_chunk_(out, of, ctx, tempAvail, tmp);
        }
        if (!dispatchedWithTempSynth) {
	  dispatch_opened_field_chunk_(out, of, ctx);
        }
        const double dispatchMs = elapsed_ms(dispatchStart);
        stat.dispatchMs += dispatchMs;
        totalDispatchMs += dispatchMs;
	
	if (needSynth && !dispatchedWithTempSynth) {
          const auto tempInputStart = Hdf5ProfileClock::now();
	  accumulate_temp_synth_inputs_(of, tempAvail, ctx, tmp);
          const double tempInputMs = elapsed_ms(tempInputStart);
          stat.tempSynthInputMs += tempInputMs;
          totalTempInputMs += tempInputMs;
	}
      }

      if (needSynth && !tmp.synthesized) {
        const auto synthStart = Hdf5ProfileClock::now();
	synthesize_temperature_chunk_(out, ctx, tempAvail, tmp);
        const double synthMs = elapsed_ms(synthStart);
        ptypeTempSynthMs += synthMs;
        totalTempSynthMs += synthMs;
      }
         
      done += n;
    }

    double ptypeReadMs = 0.0;
    double ptypeDispatchMs = 0.0;
    double ptypeTempInputMs = 0.0;
    size_t ptypeReadBytes = 0;
    for (const auto& stat : fieldStats) {
      ptypeReadMs += stat.readMs;
      ptypeDispatchMs += stat.dispatchMs;
      ptypeTempInputMs += stat.tempSynthInputMs;
      ptypeReadBytes += stat.bytes;
    }
    std::fprintf(stderr,
                 "[HDF5] ptype=%d source=%zu count=%zu written=%zu fields=%zu chunks=%zu "
                 "probe=%.3f ms openDatasets=%.3f ms prepare=%.3f ms "
                 "read=%.3f ms dispatch=%.3f ms tempInput=%.3f ms tempSynth=%.3f ms bytes=%.3f MB\n",
                 ptype,
                 subCount,
                 count_[ptype],
                 ptypeWritten,
                 opened.size(),
                 ptypeChunks,
                 probeMs,
                 openMs,
                 ptypePrepareMs,
                 ptypeReadMs,
                 ptypeDispatchMs,
                 ptypeTempInputMs,
                 ptypeTempSynthMs,
                 ptypeReadBytes / (1024.0 * 1024.0));
    for (const auto& stat : fieldStats) {
      if (stat.chunks == 0) continue;
      std::fprintf(stderr,
                   "[HDF5]   field=%s source=%s chunks=%zu elements=%zu "
                   "read=%.3f ms dispatch=%.3f ms tempInput=%.3f ms bytes=%.3f MB\n",
                   stat.fieldName.c_str(),
                   stat.sourceName.c_str(),
                   stat.chunks,
                   stat.elements,
                   stat.readMs,
                   stat.dispatchMs,
                   stat.tempSynthInputMs,
                   stat.bytes / (1024.0 * 1024.0));
    }
  }

  const double densityScaleMs = 0.0;
  const auto bfieldScaleStart = Hdf5ProfileClock::now();
  apply_bfield_scale_(out);
  const double bfieldScaleMs = elapsed_ms(bfieldScaleStart);
  
  if (masked && outWriteCursor != totalKept) {
    fprintf(stderr, "BUG: outWriteCursor(%zu) != totalKept(%zu)\n",
            outWriteCursor, totalKept);
    return false;
  }

  std::fprintf(stderr,
               "[HDF5] readRange summary requested=%zu kept=%zu chunks=%zu "
               "layout=%.3f ms mask=%.3f ms alloc=%.3f ms probe=%.3f ms "
               "openDatasets=%.3f ms read=%.3f ms dispatch=%.3f ms "
               "tempInput=%.3f ms tempSynth=%.3f ms densityScale=%.3f ms "
               "bfieldScale=%.3f ms bytes=%.3f MB total=%.3f ms\n",
               count,
               totalKept,
               totalChunks,
               layoutMs,
               maskMs,
               allocMs,
               totalProbeMs,
               totalOpenDatasetMs,
               totalReadMs,
               totalDispatchMs,
               totalTempInputMs,
               totalTempSynthMs,
               densityScaleMs,
               bfieldScaleMs,
               totalReadBytes / (1024.0 * 1024.0),
               elapsed_ms(totalStart));

  return true;
}

bool HDF5Reader::open(const std::string& path, HeaderInfo& header){
  lastError_.clear();
  const auto totalStart = Hdf5ProfileClock::now();
  // HDF5 files may omit /Parameters. Do not let units/comoving flags from the
  // previously loaded snapshot leak into this file.
  header.UnitLength_in_cm = physics_constants::pc_cm;
  header.UnitMass_in_g = physics_constants::solar_mass_g;
  header.UnitVelocity_in_cm_per_s = 1.0e5;
  header.HubbleParam = 1.0;
  header.flag_comoving = false;
  header.flag_density_in_cgs = true;
  header.flag_B_in_cgs = true;

  npart_ = 0;
  for (int t=0;t<6;++t) { mass_type_[t]=0.0; count_[t]=0; IndexStart_[t]=0; }
  IndexStart_[6]=0;

  // open file
  double fileOpenMs = 0.0;
  try {
    const auto t0 = Hdf5ProfileClock::now();
    file_ = H5::H5File(path, H5F_ACC_RDONLY);
    fileOpenMs = elapsed_ms(t0);
  } catch (const H5::FileIException& e) {
    lastError_ = "HDF5 open failed: " + e.getDetailMsg();
    std::cerr << lastError_ << "\n";
    return false;
  } catch (const H5::Exception& e) {
    lastError_ = "HDF5 open failed: " + e.getDetailMsg();
    return false;
  } catch (...) {
    lastError_ = "HDF5 open failed with unknown exception";
    return false;
  }

  dapl_ = H5Pcreate(H5P_DATASET_ACCESS);
    
  // rdcc_nslots: hash table size, roughly the expected chunk count.
  // rdcc_nbytes: total cache bytes.
  // rdcc_w0: preemption policy in [0,1].
  H5Pset_chunk_cache(dapl_, 200003,              // Prime nslots value is recommended.
		     512ULL*1024*1024,    // 512MB chunk cache
		     0.75);
    
  bool hasHeader = false;
  double headerMs = 0.0;
  const auto headerStart = Hdf5ProfileClock::now();
  try {
    H5::Group hg = file_.openGroup("/Header");
    hasHeader = true;

    double time = 0.0;
    (void)HDF5Utils::readAttributeScalar(hg, "Time", time);
    header.time = (float)time;
    header.has_redshift = false;

    (void)HDF5Utils::readAttributeScalar(hg, "BoxSize", header.boxSize);
    (void)HDF5Utils::readAttributeScalar(hg, "Omega0", header.Omega0);
    (void)HDF5Utils::readAttributeScalar(hg, "OmegaLambda", header.OmegaLambda);
    (void)HDF5Utils::readAttributeScalar(hg, "HubbleParam", header.HubbleParam);

    double z = 0.0;
    if (HDF5Utils::readAttributeScalar(hg, "Redshift", z)) {
      header.redshift = z;
      header.has_redshift = true;
    }
      
    // MassTable double[6]
    double mt[6]{};
    if (HDF5Utils::readAttributeArray(hg, "MassTable", mt)) {
      for (int t=0;t<6;++t) {
        mass_type_[t] = mt[t];
        header.massTable[t] = mt[t];
      }
    } else {
      for (int t=0;t<6;++t) {
        mass_type_[t] = 0.0;
        header.massTable[t] = 0.0;
      }
    }

    // For split Gadget/AREPO HDF5 snapshots, NumPart_Total is the whole
    // snapshot, while the datasets in this file contain NumPart_ThisFile.
    // The reader's internal count_ must therefore prefer ThisFile.
    unsigned int total32[6]{};
    bool hasNumPartTotal = false;
    if (HDF5Utils::readAttributeArray(hg, "NumPart_Total", total32)) {
      hasNumPartTotal = true;
      for (int t=0;t<6;++t) header.NumPart_ThisFile[t] = static_cast<int>(total32[t]);
    }

    bool okThisFile = false;
    {
      unsigned int n32[6]{};
      if (HDF5Utils::readAttributeArray(hg, "NumPart_ThisFile", n32)) {
        for (int t=0;t<6;++t) count_[t] = static_cast<size_t>(n32[t]);
        if (!hasNumPartTotal) {
          for (int t=0;t<6;++t) header.NumPart_ThisFile[t] = static_cast<int>(n32[t]);
        }
        okThisFile = true;
      }
    }

    if (!okThisFile) {
      if (HDF5Utils::readAttributeArray(hg, "NumPart_Total", total32)) {
        for (int t=0;t<6;++t) count_[t] = static_cast<size_t>(total32[t]);
      }
    }
  } catch (...) {
    hasHeader = false;
    header.time = 0.0f;
    header.redshift = 0.0;
    header.has_redshift = false;
    for (int t=0;t<6;++t) { mass_type_[t]=0.0; count_[t]=0; }
  }
  headerMs = elapsed_ms(headerStart);

  double parametersMs = 0.0;
  const auto parametersStart = Hdf5ProfileClock::now();
  try {
    H5::Group param = file_.openGroup("/Parameters");

    double UnitLength_in_cm = header.UnitLength_in_cm;
    if (HDF5Utils::readAttributeScalar(param, "UnitLength_in_cm", UnitLength_in_cm) &&
        std::isfinite(UnitLength_in_cm) &&
        UnitLength_in_cm > 0.0) {
      header.UnitLength_in_cm = UnitLength_in_cm;
    }

    double UnitMass_in_g = header.UnitMass_in_g;
    if (HDF5Utils::readAttributeScalar(param, "UnitMass_in_g", UnitMass_in_g) &&
        std::isfinite(UnitMass_in_g) &&
        UnitMass_in_g > 0.0) {
      header.UnitMass_in_g = UnitMass_in_g;
    }

    double UnitVelocity_in_cm_per_s = header.UnitVelocity_in_cm_per_s;
    if (HDF5Utils::readAttributeScalar(param, "UnitVelocity_in_cm_per_s", UnitVelocity_in_cm_per_s) &&
        std::isfinite(UnitVelocity_in_cm_per_s) &&
        UnitVelocity_in_cm_per_s > 0.0) {
      header.UnitVelocity_in_cm_per_s = UnitVelocity_in_cm_per_s;
    }

    double HubbleParam = header.HubbleParam;
    if (HDF5Utils::readAttributeScalar(param, "HubbleParam", HubbleParam) &&
        std::isfinite(HubbleParam) &&
        HubbleParam > 0.0) {
      header.HubbleParam = HubbleParam;
    }

    bool flag_comoving = false;
    (void)HDF5Utils::readAttributeScalar(param, "ComovingIntegrationOn", flag_comoving);
    header.flag_comoving = flag_comoving;

    bool flag_density_in_cgs = false;
    (void)HDF5Utils::readAttributeScalar(param,
                                         "FlagDensityInCgs",
                                         flag_density_in_cgs);
    header.flag_density_in_cgs = flag_density_in_cgs;

    bool flag_B_in_cgs = false;
    (void)HDF5Utils::readAttributeScalar(param,
                                         "FlagBfieldInCgs",
                                         flag_B_in_cgs);
    header.flag_B_in_cgs = flag_B_in_cgs;
  } catch (...) {
    header.flag_comoving = false;
    header.flag_density_in_cgs = true;
    header.flag_B_in_cgs = true;
  }
  parametersMs = elapsed_ms(parametersStart);

  bool allZero = true;
  for (int t=0;t<6;++t) if (count_[t] > 0) { allZero=false; break; }

  double inferCountsMs = 0.0;
  if (!hasHeader || allZero) {
    const auto inferStart = Hdf5ProfileClock::now();
    for (int t=0;t<6;++t) {
      size_t n = inferCountFromDataset(file_, t, "Coordinates");
      if (n==0) n = inferCountFromDataset(file_, t, "Velocities");
      if (n==0) n = inferCountFromDataset(file_, t, "ParticleIDs");
      count_[t] = n;
    }
    inferCountsMs = elapsed_ms(inferStart);
  }

  // prefix sum
  IndexStart_[0] = 0;
  for (int t=0;t<6;++t) IndexStart_[t+1] = IndexStart_[t] + count_[t];
  npart_ = IndexStart_[6];

  header.npart     = (int)npart_;
  header.flag_hdf5 = true;
    
  header.input_density_unit = header.flag_density_in_cgs
    ? InputDensityUnit::NumberDensityNH
    : InputDensityUnit::CodeMassDensity;
  header.input_temperature_unit = InputTemperatureUnit::Kelvin;
  header.input_magnetic_field_unit = header.flag_B_in_cgs
    ? InputMagneticFieldUnit::Gauss
    : InputMagneticFieldUnit::CodeMagneticField;
  factor_density_ = InputDensityToInternalNHFactor(header.input_density_unit,
                                                   header.UnitMass_in_g,
                                                   header.UnitLength_in_cm,
                                                   header.HubbleParam,
                                                   header.time,
                                                   header.flag_comoving);

  factor_Bfield_ = InputMagneticFieldToGaussFactor(
    header.input_magnetic_field_unit,
    header.UnitMass_in_g,
    header.UnitLength_in_cm,
    header.UnitVelocity_in_cm_per_s,
    header.HubbleParam,
    header.time,
    header.flag_comoving);

  factor_IntEnergy_ = header.UnitVelocity_in_cm_per_s*header.UnitVelocity_in_cm_per_s * physics_constants::proton_mass_cgs / physics_constants::boltzmann_cgs;    

  std::fprintf(stderr,
               "[HDF5] open path=%s npart=%zu counts=[%zu,%zu,%zu,%zu,%zu,%zu] "
               "hasHeader=%s hasRedshift=%s comoving=%s densityCgs=%s bfieldCgs=%s "
               "fileOpen=%.3f ms header=%.3f ms parameters=%.3f ms "
               "inferCounts=%.3f ms total=%.3f ms\n",
               path.c_str(),
               npart_,
               count_[0],
               count_[1],
               count_[2],
               count_[3],
               count_[4],
               count_[5],
               yes_no(hasHeader),
               yes_no(header.has_redshift),
               yes_no(header.flag_comoving),
               yes_no(header.flag_density_in_cgs),
               yes_no(header.flag_B_in_cgs),
               fileOpenMs,
               headerMs,
               parametersMs,
               inferCountsMs,
               elapsed_ms(totalStart));
    
  return true;
}

void HDF5Reader::close() {
  try { file_.close(); } catch (...) {}
  if (dapl_ != H5P_DEFAULT) {
    H5Pclose(dapl_);
    dapl_ = H5P_DEFAULT;
  }
    
  npart_ = 0;
  for (int t=0;t<6;++t) { mass_type_[t]=0.0; count_[t]=0; IndexStart_[t]=0; }
  IndexStart_[6]=0;
}

bool HDF5Reader::prepareMaskedOutput_(size_t begin, size_t count,
				      ParticleMask& mask,
				      size_t& totalKept)
{
  const size_t globalBegin = begin;
  const size_t globalEnd   = begin + count;

  if (mask.config().enableMaxParticles && mask.config().maxParticles > 0) {
    size_t thinCandidates = 0;

    for (int ptype = 0; ptype < 6; ++ptype) {
      if (flag_skip_DM_ && ptype == 1) continue;
      if (!mask.typeEnabled(ptype)) continue;
      if (!mask.typeThinOK(ptype)) continue;

      const size_t pBeg = IndexStart_[ptype];
      const size_t pEnd = IndexStart_[ptype + 1];
      if (pEnd <= globalBegin || globalEnd <= pBeg) continue;

      const size_t subBegG = std::max(globalBegin, pBeg);
      const size_t subEndG = std::min(globalEnd,   pEnd);
      thinCandidates += (subEndG - subBegG);
    }

    mask.prepare(thinCandidates);
  } else {
    mask.prepare(0);
  }

  totalKept = 0;
  std::vector<uint32_t> keep;

  for (int ptype = 0; ptype < 6; ++ptype) {
    if (flag_skip_DM_ && ptype == 1) continue;
    if (!mask.typeEnabled(ptype)) continue;

    const size_t pBeg = IndexStart_[ptype];
    const size_t pEnd = IndexStart_[ptype + 1];
    if (pEnd <= globalBegin || globalEnd <= pBeg) continue;

    const size_t subBegG = std::max(globalBegin, pBeg);
    const size_t subEndG = std::min(globalEnd,   pEnd);
    const size_t subCount = subEndG - subBegG;
    const size_t localStart0 = subBegG - pBeg;

    if (mask.typePassesAll(ptype)) {
      totalKept += subCount;
      continue;
    }

    size_t done = 0;
    while (done < subCount) {
      const size_t n = std::min(blockSize_, subCount - done);
      if (!build_keep_chunk_(ptype, localStart0 + done, n, mask, keep))
        return false;
      totalKept += keep.size();
      done += n;
    }
  }

  return true;
}

bool HDF5Reader::build_opened_fields_for_ptype_(int ptype,
						BinaryReadLayout& layout,
						std::vector<OpenedField>& opened)
{
  opened.clear();
  opened.reserve(layout.fields.size());

  for (auto& fl : layout.fields) {
    if (fl.dest == DestKind::Ignore && !isTempSynthField(fl.ftype)) continue;
    if (!fl.present) continue;

    const std::string dsName = fl.spec.sourceName;
    try {
      H5SilenceErrors quiet(ptype >= 1);
      H5::DataSet ds = openDataSetWithDAPL(partPath(ptype, dsName));

      update_layout_from_hdf5(fl, ds);
      fl.present = true;

      OpenedField of;
      of.fl     = &fl;
      of.ds     = std::move(ds);
      of.elemSz = dataTypeSize(fl.spec.type);
      of.comps  = fl.spec.count;
      of.bpp    = of.elemSz * static_cast<size_t>(of.comps);

      opened.push_back(std::move(of));
    } catch (...) {
      // This particle type does not have that field.
    }
  }

  return true;
}

bool HDF5Reader::prepare_chunk_context_(ChunkContext& ctx,
                                        ParticleMask* mask,
                                        std::vector<uint32_t>& keep,
                                        size_t& outWriteCursor)
{
  ctx.outBase = 0;
  ctx.nwrite = 0;
  ctx.contiguousKeep = false;
  ctx.keepPtr = nullptr;

  if (!ctx.masked) {
    ctx.outBase = ctx.outStart + ctx.done;
    ctx.nwrite  = ctx.nread;
    return true;
  }

  if (!mask) return false;

  if (mask->typePassesAll(ctx.ptype)) {
    ctx.outBase = outWriteCursor;
    ctx.nwrite = ctx.nread;
    ctx.contiguousKeep = true;
    outWriteCursor += ctx.nread;
    return true;
  }

  if (!build_keep_chunk_(ctx.ptype, ctx.localStart + ctx.done, ctx.nread, *mask, keep))
    return false;

  ctx.keepPtr = &keep;
  ctx.outBase = outWriteCursor;
  ctx.nwrite  = keep.size();
  if (ctx.nwrite == ctx.nread) {
    ctx.contiguousKeep = true;
    for (size_t i = 0; i < keep.size(); ++i) {
      if (keep[i] != i) {
        ctx.contiguousKeep = false;
        break;
      }
    }
  }

  outWriteCursor += keep.size();
  return true;
}

void HDF5Reader::dispatch_opened_field_chunk_(SimulationBlock& out,
                                              const OpenedField& of,
                                              const ChunkContext& ctx)
{
  if (of.fl->dest == DestKind::Ignore) return;

  if (of.fl->dest == DestKind::AoSCore) {
    if (!ctx.masked || ctx.contiguousKeep) {
      if (of.fl->spec.type == DataType::Float) {
        const float* src = reinterpret_cast<const float*>(of.buf.data());
        switch (of.fl->ftype) {
        case FieldKey::Position:
          for (size_t j = 0; j < ctx.nread; ++j) {
            SimulationElement& p = out.particles[ctx.outBase + j];
            p.position[0] = src[3 * j + 0];
            p.position[1] = src[3 * j + 1];
            p.position[2] = src[3 * j + 2];
          }
          return;
        case FieldKey::Velocity:
          for (size_t j = 0; j < ctx.nread; ++j) {
            SimulationElement& p = out.particles[ctx.outBase + j];
            p.vel[0] = src[3 * j + 0];
            p.vel[1] = src[3 * j + 1];
            p.vel[2] = src[3 * j + 2];
          }
          return;
        case FieldKey::Mass:
          for (size_t j = 0; j < ctx.nread; ++j) {
            out.particles[ctx.outBase + j].mass = src[j];
          }
          return;
        case FieldKey::Density:
          if (factor_density_ == 1.0) {
            for (size_t j = 0; j < ctx.nread; ++j) {
              out.particles[ctx.outBase + j].density = src[j];
            }
          } else {
            const float densityFactor = static_cast<float>(factor_density_);
            for (size_t j = 0; j < ctx.nread; ++j) {
              out.particles[ctx.outBase + j].density = src[j] * densityFactor;
            }
          }
          return;
        case FieldKey::Temperature:
          for (size_t j = 0; j < ctx.nread; ++j) {
            out.particles[ctx.outBase + j].temperature = src[j];
          }
          return;
        case FieldKey::Hsml:
          for (size_t j = 0; j < ctx.nread; ++j) {
            out.particles[ctx.outBase + j].supportRadius = src[j];
          }
          return;
        case FieldKey::Volume:
          for (size_t j = 0; j < ctx.nread; ++j) {
            out.particles[ctx.outBase + j].supportRadius =
              fast_cbrt_positive_float(src[j]);
          }
          return;
        default:
          break;
        }
      } else if (of.fl->spec.type == DataType::Double) {
        const double* src = reinterpret_cast<const double*>(of.buf.data());
        switch (of.fl->ftype) {
        case FieldKey::Position:
          for (size_t j = 0; j < ctx.nread; ++j) {
            SimulationElement& p = out.particles[ctx.outBase + j];
            p.position[0] = static_cast<float>(src[3 * j + 0]);
            p.position[1] = static_cast<float>(src[3 * j + 1]);
            p.position[2] = static_cast<float>(src[3 * j + 2]);
          }
          return;
        case FieldKey::Velocity:
          for (size_t j = 0; j < ctx.nread; ++j) {
            SimulationElement& p = out.particles[ctx.outBase + j];
            p.vel[0] = static_cast<float>(src[3 * j + 0]);
            p.vel[1] = static_cast<float>(src[3 * j + 1]);
            p.vel[2] = static_cast<float>(src[3 * j + 2]);
          }
          return;
        case FieldKey::Mass:
          for (size_t j = 0; j < ctx.nread; ++j) {
            out.particles[ctx.outBase + j].mass = static_cast<float>(src[j]);
          }
          return;
        case FieldKey::Density:
          if (factor_density_ == 1.0) {
            for (size_t j = 0; j < ctx.nread; ++j) {
              out.particles[ctx.outBase + j].density = static_cast<float>(src[j]);
            }
          } else {
            const double densityFactor = factor_density_;
            for (size_t j = 0; j < ctx.nread; ++j) {
              out.particles[ctx.outBase + j].density =
                static_cast<float>(src[j] * densityFactor);
            }
          }
          return;
        case FieldKey::Temperature:
          for (size_t j = 0; j < ctx.nread; ++j) {
            out.particles[ctx.outBase + j].temperature = static_cast<float>(src[j]);
          }
          return;
        case FieldKey::Hsml:
          for (size_t j = 0; j < ctx.nread; ++j) {
            out.particles[ctx.outBase + j].supportRadius = static_cast<float>(src[j]);
          }
          return;
        case FieldKey::Volume:
          for (size_t j = 0; j < ctx.nread; ++j) {
            out.particles[ctx.outBase + j].supportRadius =
              fast_cbrt_positive_float(static_cast<float>(src[j]));
          }
          return;
        default:
          break;
        }
      }

      for (size_t j = 0; j < ctx.nread; ++j) {
        SimulationElement& p = out.particles[ctx.outBase + j];
        const uint8_t* src = of.buf.data() + j * of.bpp;
        of.fl->store(p, src);
      }
      if (of.fl->ftype == FieldKey::Density) {
        apply_density_scale_chunk_(out, ctx.outBase, ctx.nread);
      }
    } else {
      for (size_t kk = 0; kk < ctx.keepPtr->size(); ++kk) {
        const uint32_t j = (*ctx.keepPtr)[kk];
        SimulationElement& p = out.particles[ctx.outBase + kk];
        const uint8_t* src = of.buf.data() + (size_t)j * of.bpp;
        of.fl->store(p, src);
      }
      if (of.fl->ftype == FieldKey::Density) {
        apply_density_scale_chunk_(out, ctx.outBase, ctx.nwrite);
      }
    }
    return;
  }

  if (of.fl->dest == DestKind::SoA) {
    copy_chunk_to_soa(out, *of.fl, ctx.outBase, ctx.nread, of.bpp, of.buf,
                      ctx.masked && !ctx.contiguousKeep, ctx.keepPtr);
    return;
  }

  if (!ctx.masked || ctx.contiguousKeep) {
    for (size_t j = 0; j < ctx.nread; ++j) {
      const size_t oi = ctx.outBase + j;
      const uint8_t* src = of.buf.data() + j * of.bpp;
      switch (of.fl->spec.type) {
        case DataType::Float:  writeFieldToSimulationBlock(out, oi, *of.fl, reinterpret_cast<const float*>(src)); break;
        case DataType::Double: writeFieldToSimulationBlock(out, oi, *of.fl, reinterpret_cast<const double*>(src)); break;
        case DataType::Int32:  writeFieldToSimulationBlock(out, oi, *of.fl, reinterpret_cast<const int32_t*>(src)); break;
        case DataType::Int64:  writeFieldToSimulationBlock(out, oi, *of.fl, reinterpret_cast<const int64_t*>(src)); break;
      }
    }
  } else {
    for (size_t kk = 0; kk < ctx.keepPtr->size(); ++kk) {
      const uint32_t j = (*ctx.keepPtr)[kk];
      const size_t oi = ctx.outBase + kk;
      const uint8_t* src = of.buf.data() + (size_t)j * of.bpp;
      switch (of.fl->spec.type) {
        case DataType::Float:  writeFieldToSimulationBlock(out, oi, *of.fl, reinterpret_cast<const float*>(src)); break;
        case DataType::Double: writeFieldToSimulationBlock(out, oi, *of.fl, reinterpret_cast<const double*>(src)); break;
        case DataType::Int32:  writeFieldToSimulationBlock(out, oi, *of.fl, reinterpret_cast<const int32_t*>(src)); break;
        case DataType::Int64:  writeFieldToSimulationBlock(out, oi, *of.fl, reinterpret_cast<const int64_t*>(src)); break;
      }
    }
  }
}

void HDF5Reader::accumulate_temp_synth_inputs_(const OpenedField& of,
                                               const TempSynthAvailability& tempAvail,
                                               const ChunkContext& ctx,
                                               TempSynthBuffers& tmp)
{
  (void)ctx;

  if (of.fl->ftype == FieldKey::InternalEnergy) {
    tmp.u = &of;
  } else if (tempAvail.hasE && of.fl->ftype == FieldKey::ElectronAbundance) {
    tmp.e = &of;
  } else if (tempAvail.hasH2 && of.fl->ftype == FieldKey::H2Abundance) {
    tmp.h2 = &of;
  } else if (tempAvail.hasG && of.fl->ftype == FieldKey::Gamma) {
    tmp.g = &of;
  }
}

bool HDF5Reader::dispatch_h2_and_temperature_chunk_(
  SimulationBlock& out,
  const OpenedField& of,
  const ChunkContext& ctx,
  const TempSynthAvailability& tempAvail,
  TempSynthBuffers& tmp)
{
  if (tmp.synthesized) return false;
  if (!tmp.u) return false;
  if (tempAvail.hasE || tempAvail.hasG) return false;
  if (!tempAvail.hasH2) return false;
  if (of.fl->ftype != FieldKey::H2Abundance) return false;
  if (of.fl->dest != DestKind::SoA) return false;
  if (of.fl->spec.type != tmp.u->fl->spec.type) {
    return false;
  }

  auto it = out.soa.find(of.fl->soaKey);
  if (it == out.soa.end()) return false;

  const bool direct = !ctx.masked || ctx.contiguousKeep;

  auto synth = [&](auto scalarTag) {
    using Scalar = decltype(scalarTag);
    if (of.bpp != sizeof(Scalar) || tmp.u->bpp != sizeof(Scalar)) {
      return false;
    }

    auto& soa = it->second;
    auto* dstH2 = reinterpret_cast<Scalar*>(soa.bytes.data() +
                                            ctx.outBase * sizeof(Scalar));
    const auto* h2 = reinterpret_cast<const Scalar*>(of.buf.data());
    const auto* u = reinterpret_cast<const Scalar*>(tmp.u->buf.data());
    const Scalar intEnergyFactor = static_cast<Scalar>(factor_IntEnergy_);

    for (size_t kk = 0; kk < ctx.nwrite; ++kk) {
      const size_t sourceIndex =
        direct ? kk : static_cast<size_t>((*ctx.keepPtr)[kk]);
      const Scalar h2Val = h2[sourceIndex];
      dstH2[kk] = h2Val;

      const Scalar uVal = u[sourceIndex];
      if (!(uVal > static_cast<Scalar>(0))) continue;

      const Scalar denom =
        static_cast<Scalar>(1.2) -
        ((h2Val > static_cast<Scalar>(0)) ? h2Val : static_cast<Scalar>(0));
      const Scalar T =
        (static_cast<Scalar>(2.0) / static_cast<Scalar>(3.0)) *
        uVal / denom * intEnergyFactor;
      if (T > static_cast<Scalar>(0)) {
        out.particles[ctx.outBase + kk].temperature = static_cast<float>(T);
      }
    }
    return true;
  };

  bool ok = false;
  if (of.fl->spec.type == DataType::Float) {
    ok = synth(float{});
  } else if (of.fl->spec.type == DataType::Double) {
    ok = synth(double{});
  } else {
    return false;
  }
  if (!ok) return false;

  tmp.h2 = &of;
  tmp.synthesized = true;
  return true;
}

HDF5Reader::TempSynthAvailability
HDF5Reader::probe_temp_synth_availability_(int ptype,
					   const std::vector<OpenedField>& opened,
					   const TempSynthRequest& req) const
{
  TempSynthAvailability avail;

  if (ptype != 0) return avail;

  auto isPresent = [&](FieldKey ft) -> bool {
    for (const OpenedField& of : opened) {
      if (!of.fl) continue;
      if (of.fl->ftype == ft) return true;
    }
    return false;
  };

  if (req.wantTemp) avail.hasTemp = isPresent(FieldKey::Temperature);
  if (req.wantU)    avail.hasU    = isPresent(FieldKey::InternalEnergy);
  if (req.wantE)    avail.hasE    = isPresent(FieldKey::ElectronAbundance);
  if (req.wantH2)   avail.hasH2   = isPresent(FieldKey::H2Abundance);
  if (req.wantG)    avail.hasG    = isPresent(FieldKey::Gamma);

  avail.needSynth = (!avail.hasTemp && avail.hasU);
  return avail;
}

void HDF5Reader::synthesize_temperature_chunk_(SimulationBlock& out,
                                               const ChunkContext& ctx,
                                               const TempSynthAvailability& tempAvail,
                                               const TempSynthBuffers& tmp)
{
  if (!tmp.u) return;

  if (tmp.u->fl->spec.type == DataType::Float &&
      (!tempAvail.hasE || (tmp.e && tmp.e->fl->spec.type == DataType::Float)) &&
      (!tempAvail.hasH2 || (tmp.h2 && tmp.h2->fl->spec.type == DataType::Float)) &&
      (!tempAvail.hasG || (tmp.g && tmp.g->fl->spec.type == DataType::Float))) {
    const auto* u = reinterpret_cast<const float*>(tmp.u->buf.data());
    const auto* e = tmp.e ? reinterpret_cast<const float*>(tmp.e->buf.data()) : nullptr;
    const auto* h2 = tmp.h2 ? reinterpret_cast<const float*>(tmp.h2->buf.data()) : nullptr;
    const auto* g = tmp.g ? reinterpret_cast<const float*>(tmp.g->buf.data()) : nullptr;
    const float intEnergyFactor = static_cast<float>(factor_IntEnergy_);
    const bool direct = !ctx.masked || ctx.contiguousKeep;

    for (size_t kk = 0; kk < ctx.nwrite; ++kk) {
      const size_t sourceIndex =
        direct ? kk : static_cast<size_t>((*ctx.keepPtr)[kk]);
      const float uVal = u[sourceIndex];
      if (!(uVal > 0.0f)) continue;

      float gamma = 5.0f / 3.0f;
      if (g && g[sourceIndex] > 0.0f) gamma = g[sourceIndex];

      float denom = 1.2f;
      if (e && e[sourceIndex] > 0.0f) denom += e[sourceIndex];
      if (h2 && h2[sourceIndex] > 0.0f) denom -= h2[sourceIndex];

      const float T = (gamma - 1.0f) * uVal / denom * intEnergyFactor;
      if (T > 0.0f) {
        out.particles[ctx.outBase + kk].temperature = T;
      }
    }
    return;
  }

  auto read_scalar = [](const OpenedField* field, size_t sourceIndex)->double {
    if (!field) return -1.0;
    const uint8_t* p = field->buf.data() + sourceIndex * field->bpp;
    switch (field->fl->spec.type) {
      case DataType::Float:  { float   v; std::memcpy(&v, p, sizeof(v)); return static_cast<double>(v); }
      case DataType::Double: { double  v; std::memcpy(&v, p, sizeof(v)); return v; }
      case DataType::Int32:  { int32_t v; std::memcpy(&v, p, sizeof(v)); return static_cast<double>(v); }
      case DataType::Int64:  { int64_t v; std::memcpy(&v, p, sizeof(v)); return static_cast<double>(v); }
    }
    return -1.0;
  };

  for (size_t kk = 0; kk < ctx.nwrite; ++kk) {
    const size_t sourceIndex =
      (!ctx.masked || ctx.contiguousKeep) ? kk : static_cast<size_t>((*ctx.keepPtr)[kk]);
    const double u = read_scalar(tmp.u, sourceIndex);
    if (!(u > 0.0)) continue;

    double gamma = 5.0 / 3.0;
    const double g = tempAvail.hasG ? read_scalar(tmp.g, sourceIndex) : -1.0;
    if (g > 0.0) gamma = g;

    double denom = 1.2;
    const double e = tempAvail.hasE ? read_scalar(tmp.e, sourceIndex) : -1.0;
    const double h2 = tempAvail.hasH2 ? read_scalar(tmp.h2, sourceIndex) : -1.0;
    if (e > 0.0) denom += e;
    if (h2 > 0.0) denom -= h2;

    const double T = (gamma - 1.0) * u / denom * factor_IntEnergy_;
    if (T > 0.0) {
      out.particles[ctx.outBase + kk].temperature = (float)T;
    }
  }
}

HDF5Reader::TempSynthRequest
HDF5Reader::build_temp_synth_request_(const BinaryReadLayout& layout) const
{
  TempSynthRequest req;

  for (const auto& fl : layout.fields) {
    if (fl.dest == DestKind::Ignore && !isTempSynthField(fl.ftype)) continue;

    switch (fl.ftype) {
    case FieldKey::Temperature:       req.wantTemp = true; break;
    case FieldKey::InternalEnergy:    req.wantU    = true; break;
    case FieldKey::ElectronAbundance: req.wantE    = true; break;
    case FieldKey::H2Abundance:       req.wantH2   = true; break;
    case FieldKey::Gamma:             req.wantG    = true; break;
    default: break;
    }
  }

  return req;
}

void HDF5Reader::init_temp_synth_buffers_(HDF5Reader::TempSynthBuffers& tmp,
					  const HDF5Reader::TempSynthAvailability& tempAvail,
					  size_t nwrite)
{
  (void)tempAvail;
  (void)nwrite;
  tmp.u = nullptr;
  tmp.e = nullptr;
  tmp.h2 = nullptr;
  tmp.g = nullptr;
  tmp.synthesized = false;
}

bool HDF5Reader::finalize_layout_from_hdf5_(BinaryReadLayout& layout)
{
  for (auto& fl : layout.fields) {
    if (fl.dest == DestKind::Ignore && !isTempSynthField(fl.ftype)) {
      fl.present = false;
      continue;
    }

    const std::string dsName = fl.spec.sourceName;
    bool found = false;

    for (int ptype = 0; ptype < 6; ++ptype) {
      if (flag_skip_DM_ && ptype == 1) continue;

      try {
        H5SilenceErrors quiet(ptype >= 1);
        H5::DataSet ds = openDataSetWithDAPL(partPath(ptype, dsName));
        update_layout_from_hdf5(fl, ds);
        found = true;
        break;
      } catch (...) {
      }
    }

    fl.present = found;
  }

  return true;
}

void HDF5Reader::allocate_output_from_layout_(SimulationBlock& out,
                                  const BinaryReadLayout& layout,
                                  size_t totalCount)
{
  out.clear();
  out.resize(totalCount);

  for (const auto& fl : layout.fields) {
    if (fl.dest != DestKind::SoA) continue;
    if (!fl.present) continue;

    auto& f = out.soa[fl.soaKey];
    f.type  = fl.spec.type;
    f.comps = fl.spec.count;
    f.resize(totalCount);
  }
}

bool HDF5Reader::read_coords_chunk_(int ptype, size_t localStart, size_t n,
				    std::vector<uint8_t>& buf)
{
  try{
    H5::DataSet ds = openDataSetWithDAPL(partPath(ptype, "Coordinates"));
    const H5::PredType memType = H5::PredType::NATIVE_FLOAT;
    const size_t elemSz = sizeof(float);
    const int comps = 3;
    HDF5Utils::readHyperslabBytes(ds, memType, (hsize_t)localStart, (hsize_t)n, comps, buf, elemSz);
    return true;
  }catch(...){
    return false;
  }
}

bool HDF5Reader::read_ids_chunk_u64_(int ptype, size_t localStart, size_t n,
				     std::vector<uint64_t>& ids)
{
  ids.resize(n);
  try{
    H5::DataSet ds = openDataSetWithDAPL(partPath(ptype, "ParticleIDs"));

    H5::DataSpace fsp = ds.getSpace();
    hsize_t start[1] = { (hsize_t)localStart };
    hsize_t cnt[1]   = { (hsize_t)n };
    fsp.selectHyperslab(H5S_SELECT_SET, cnt, start);

    H5::DataSpace msp(1, cnt);
    ds.read(ids.data(), H5::PredType::NATIVE_ULLONG, msp, fsp);
    return true;
  }catch(...){
    // Worst-case fallback.
    for(size_t j=0;j<n;++j){
      ids[j] = (uint64_t)(IndexStart_[ptype] + localStart + j + 1);
    }
    return true;
  }
}

bool HDF5Reader::build_keep_chunk_(int ptype, size_t localStart, size_t n,
				   ParticleMask& mask,
				   std::vector<uint32_t>& keep)
{
  keep.clear();
  keep.reserve(n);

  std::vector<uint8_t> coordBuf;
  std::vector<uint64_t> ids;

  const bool needPos = mask.needPos();
  const bool needID  = mask.needID();

  const float* xyz = nullptr;
  if(needPos){
    if(!read_coords_chunk_(ptype, localStart, n, coordBuf)) return false;
    xyz = reinterpret_cast<const float*>(coordBuf.data());
  }
  if(needID){
    if(!read_ids_chunk_u64_(ptype, localStart, n, ids)) return false;
  }

  for(uint32_t j=0; j<(uint32_t)n; ++j){
    CoreSample c;
    c.type = ptype;

    if(needPos){
      c.pos[0] = xyz[3*(size_t)j + 0];
      c.pos[1] = xyz[3*(size_t)j + 1];
      c.pos[2] = xyz[3*(size_t)j + 2];
    }else{
      c.pos[0]=c.pos[1]=c.pos[2]=0.0;
    }

    c.id = needID ? ids[j] : (uint64_t)(IndexStart_[ptype] + localStart + j + 1);

    if(mask.pass(c)) keep.push_back(j);
  }
  return true;
}

void HDF5Reader::apply_density_scale_(SimulationBlock& out)
{
  if (factor_density_ == 1.0) return;
  
  for (auto& p : out.particles) {
    p.density = (float)((double)p.density * factor_density_);
  }
}

void HDF5Reader::apply_density_scale_chunk_(SimulationBlock& out,
                                            size_t outBase,
                                            size_t nwrite) const
{
  if (factor_density_ == 1.0) return;

  const float densityFactor = static_cast<float>(factor_density_);
  for (size_t i = 0; i < nwrite; ++i) {
    out.particles[outBase + i].density *= densityFactor;
  }
}

void HDF5Reader::apply_bfield_scale_(SimulationBlock& out)
{
  if (factor_Bfield_ == 1.0) return;

  auto it = out.soa.find("Bfield");
  if (it == out.soa.end()) return;

  auto& f = it->second;
  const size_t n = out.particles.size();
  const int comps = f.comps;

  if (f.type == DataType::Float) {
    float* p = reinterpret_cast<float*>(f.bytes.data());
    const float fac = (float)factor_Bfield_;
    for (size_t i = 0; i < n * (size_t)comps; ++i) {
      p[i] *= fac;
    }
  } else if (f.type == DataType::Double) {
    double* p = reinterpret_cast<double*>(f.bytes.data());
    const double fac = factor_Bfield_;
    for (size_t i = 0; i < n * (size_t)comps; ++i) {
      p[i] *= fac;
    }
  }
}

void HDF5Reader::initialize_particle_defaults_chunk_(SimulationBlock& out,
						     int ptype,
						     size_t outBase,
						     size_t nwrite)
{
  for (size_t kk = 0; kk < nwrite; ++kk) {
    SimulationElement& p = out.particles[outBase + kk];
    p.type = ptype;
    if (mass_type_[ptype] > 0.0) {
      p.mass = (float)mass_type_[ptype];
    }
  }
}

bool HDF5Reader::isTempSynthField(FieldKey ft){
  switch(ft){
  case FieldKey::InternalEnergy:
  case FieldKey::ElectronAbundance:
  case FieldKey::H2Abundance:
  case FieldKey::Gamma:
    return true;
  default:
    return false;
  }
}  

#endif
 
