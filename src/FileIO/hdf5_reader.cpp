#ifdef HAVE_HDF5

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "FileIO/hdf5_reader.h"
#include "FileIO/file_layout.h"
#include "FileIO/file_mask.h"
#include "data/header_info.h"

namespace {
  std::string partPath(int ptype, const std::string& dsName) {
    return "/PartType" + std::to_string(ptype) + "/" + dsName;
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
    case FieldKey::ID:          fl.store = isI32 ? &store_id_i32 : (isI64 ? &store_id_i64 : nullptr); break;
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

  void copy_chunk_to_soa(ParticleBlock& out,
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

bool HDF5Reader::readRange(ParticleBlock& out,
			   size_t begin, size_t count,
			   const std::vector<FieldSpec>& fields,
			   ParticleMask* mask)
{
  if (begin + count > npart_) return false;

  BinaryReadLayout layout = buildBinaryReadLayout(fields, true);
  const bool masked = (mask != nullptr) && mask->active();

  size_t totalKept = count;
  if (masked) {
    if (!prepareMaskedOutput_(begin, count, *mask, totalKept))
      return false;
  }

  const size_t globalBegin = begin;
  const size_t globalEnd   = begin + count;

  if (!finalize_layout_from_hdf5_(layout))
    return false;

  allocate_output_from_layout_(out, layout, totalKept);

  const TempSynthRequest tempReq = build_temp_synth_request_(layout);

  size_t outWriteCursor = 0;

  for (int ptype = 0; ptype < 6; ++ptype) {
    if (flag_skip_DM_ && ptype == 1) continue;

    const TempSynthAvailability tempAvail =
      probe_temp_synth_availability_(ptype, layout, tempReq);
    
    const bool needSynth = tempAvail.needSynth;
    
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

    std::vector<OpenedField> opened;
    if (!build_opened_fields_for_ptype_(ptype, layout, opened))
      return false;
    
    while (done < subCount) {      
      const size_t n = std::min(blockSize_, subCount - done);
      
      ChunkContext ctx;
      ctx.ptype      = ptype;
      ctx.localStart = localStart;
      ctx.done       = done;
      ctx.nread      = n;
      ctx.outStart   = outStart;
      ctx.masked     = masked;

      if (!prepare_chunk_context_(ctx, mask, keep, outWriteCursor))
	return false;

      if (ctx.nwrite == 0) {
	done += n;
	continue;
      }

      initialize_particle_defaults_chunk_(out, ptype, ctx.outBase, ctx.nwrite);

      TempSynthBuffers tmp;
      if (needSynth)
	init_temp_synth_buffers_(tmp, tempAvail, ctx.nwrite);
      
      for (auto& of : opened) {
        of.buf.resize(n * of.bpp);
        const H5::PredType memType = h5_memtype_from_source(of.fl->spec.type);

        HDF5Utils::readHyperslabBytes(of.ds, memType,
				      (hsize_t)(ctx.localStart + ctx.done),
				      (hsize_t)ctx.nread,
				      of.comps, of.buf, of.elemSz);
	
	dispatch_opened_field_chunk_(out, of, ctx);
	
	if (needSynth) {
	  accumulate_temp_synth_inputs_(of, tempAvail, ctx, tmp);
	}
      }

      if (needSynth) {
	synthesize_temperature_chunk_(out, ctx, tempAvail, tmp);
      }
         
      done += n;
    }
  }

  apply_density_scale_(out);
  apply_bfield_scale_(out);
  
  if (masked && outWriteCursor != totalKept) {
    fprintf(stderr, "BUG: outWriteCursor(%zu) != totalKept(%zu)\n",
            outWriteCursor, totalKept);
    return false;
  }

  return true;
}

bool HDF5Reader::open(const std::string& path, HeaderInfo& header){
  npart_ = 0;
  for (int t=0;t<6;++t) { mass_type_[t]=0.0; count_[t]=0; IndexStart_[t]=0; }
  IndexStart_[6]=0;

  // open file
  try {
    file_ = H5::H5File(path, H5F_ACC_RDONLY);
  } catch (const H5::FileIException& e) {
    std::cerr << "HDF5 open failed: " << e.getDetailMsg() << "\n";
    return false;
  } catch (const H5::Exception& e) {
    return false;
  } catch (...) {
    return false;
  }

  dapl_ = H5Pcreate(H5P_DATASET_ACCESS);
    
  // rdcc_nslots: ハッシュ表サイズ（chunk数の目安）
  // rdcc_nbytes: キャッシュ総バイト数
  // rdcc_w0: preemption policy（0..1）
  H5Pset_chunk_cache(dapl_, 200003,              // nslots（素数が推奨）
		     512ULL*1024*1024,    // 512MB chunk cache
		     0.75);
    
  bool hasHeader = false;
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

    double z = 0.0;
    if (HDF5Utils::readAttributeScalar(hg, "Redshift", z)) {
      header.redshift = z;
      header.has_redshift = true;
    }
      
    // MassTable double[6]
    double mt[6]{};
    if (HDF5Utils::readAttributeArray(hg, "MassTable", mt)) {
      for (int t=0;t<6;++t) mass_type_[t] = mt[t];
    } else {
      for (int t=0;t<6;++t) mass_type_[t] = 0.0;
    }

    // NumPart_Total (uint32)
    bool okNum = false;
    {
      unsigned int       n32[6]{};
      if (HDF5Utils::readAttributeArray(hg, "NumPart_Total", n32)) {
	for (int t=0;t<6;++t) count_[t] = (size_t)n32[t];
	okNum = true;
      }
    }

    if (!okNum) {
      unsigned int       n32[6]{};
      if (HDF5Utils::readAttributeArray(hg, "NumPart_ThisFile", n32)) {
	for (int t=0;t<6;++t) count_[t] = (size_t)n32[t];
	okNum = true;
      }
    }
  } catch (...) {
    hasHeader = false;
    header.time = 0.0f;
    header.redshift = 0.0;
    header.has_redshift = false;
    for (int t=0;t<6;++t) { mass_type_[t]=0.0; count_[t]=0; }
  }

  bool hasParam = false;
  try {
    H5::Group param = file_.openGroup("/Parameters");
    hasParam = true;

    double UnitLength_in_cm;
    (void)HDF5Utils::readAttributeScalar(param, "UnitLength_in_cm", UnitLength_in_cm);
    header.UnitLength_in_cm = UnitLength_in_cm;

    double UnitMass_in_g;
    (void)HDF5Utils::readAttributeScalar(param, "UnitMass_in_g", UnitMass_in_g);
    header.UnitMass_in_g = UnitMass_in_g;

    double UnitVelocity_in_cm_per_s;
    (void)HDF5Utils::readAttributeScalar(param, "UnitVelocity_in_cm_per_s", UnitVelocity_in_cm_per_s);
    header.UnitVelocity_in_cm_per_s = UnitVelocity_in_cm_per_s;

    double HubbleParam;
    (void)HDF5Utils::readAttributeScalar(param, "HubbleParam", HubbleParam);
    header.HubbleParam = HubbleParam;

    bool flag_comoving = false;
    (void)HDF5Utils::readAttributeScalar(param, "ComovingIntegrationOn", flag_comoving);
    header.flag_comoving = flag_comoving;

    bool flag_density_in_cgs = false;
    (void)HDF5Utils::readAttributeScalar(param, "FlagDensityInCgs", flag_density_in_cgs);
    header.flag_density_in_cgs = flag_density_in_cgs;

    bool flag_B_in_cgs = false;
    (void)HDF5Utils::readAttributeScalar(param, "FlagBfieldInCgs", flag_B_in_cgs);
    header.flag_B_in_cgs = flag_B_in_cgs;
  } catch (...) {
    hasParam = false;
    header.flag_comoving = false;
    header.flag_density_in_cgs = false;
    header.flag_B_in_cgs = false;
  }

  bool allZero = true;
  for (int t=0;t<6;++t) if (count_[t] > 0) { allZero=false; break; }

  if (!hasHeader || allZero) {
    for (int t=0;t<6;++t) {
      size_t n = inferCountFromDataset(file_, t, "Coordinates");
      if (n==0) n = inferCountFromDataset(file_, t, "Velocities");
      if (n==0) n = inferCountFromDataset(file_, t, "ParticleIDs");
      count_[t] = n;
    }
  }

  // prefix sum
  IndexStart_[0] = 0;
  for (int t=0;t<6;++t) IndexStart_[t+1] = IndexStart_[t] + count_[t];
  npart_ = IndexStart_[6];

  header.npart     = (int)npart_;
  header.flag_hdf5 = true;
    
  factor_density_ = 1.;
  if(header.flag_density_in_cgs == false){
    factor_density_ = header.HubbleParam * header.HubbleParam * header.UnitMass_in_g / pow(header.UnitLength_in_cm, 3) / physics_constants::proton_mass_cgs;
    if(header.flag_comoving)
      factor_density_ /= pow(header.time, 3);
  }

  factor_Bfield_ = 1.;
  if(header.flag_B_in_cgs == false){
    factor_Bfield_ = sqrt(header.UnitMass_in_g / header.UnitLength_in_cm) / (header.UnitLength_in_cm / header.UnitVelocity_in_cm_per_s / header.HubbleParam);
    if(header.flag_comoving)
      factor_Bfield_ /= pow(header.time, 2);
  }

  factor_IntEnergy_ = header.UnitVelocity_in_cm_per_s*header.UnitVelocity_in_cm_per_s * physics_constants::proton_mass_cgs / physics_constants::boltzmann_cgs;    
    
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

      OpenedField of;
      of.fl     = &fl;
      of.ds     = std::move(ds);
      of.elemSz = dataTypeSize(fl.spec.type);
      of.comps  = fl.spec.count;
      of.bpp    = of.elemSz * static_cast<size_t>(of.comps);

      opened.push_back(std::move(of));
    } catch (...) {
      // この ptype にはその field が無い
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
  ctx.keepPtr = nullptr;

  if (!ctx.masked) {
    ctx.outBase = ctx.outStart + ctx.done;
    ctx.nwrite  = ctx.nread;
    return true;
  }

  if (!mask) return false;

  if (!build_keep_chunk_(ctx.ptype, ctx.localStart + ctx.done, ctx.nread, *mask, keep))
    return false;

  ctx.keepPtr = &keep;
  ctx.outBase = outWriteCursor;
  ctx.nwrite  = keep.size();

  outWriteCursor += keep.size();
  return true;
}

void HDF5Reader::dispatch_opened_field_chunk_(ParticleBlock& out,
                                              const OpenedField& of,
                                              const ChunkContext& ctx)
{
  if (of.fl->dest == DestKind::Ignore) return;

  if (of.fl->dest == DestKind::AoSCore) {
    if (!ctx.masked) {
      for (size_t j = 0; j < ctx.nread; ++j) {
        ParticleData& p = out.particles[ctx.outBase + j];
        const uint8_t* src = of.buf.data() + j * of.bpp;
        of.fl->store(p, src);
      }
    } else {
      for (size_t kk = 0; kk < ctx.keepPtr->size(); ++kk) {
        const uint32_t j = (*ctx.keepPtr)[kk];
        ParticleData& p = out.particles[ctx.outBase + kk];
        const uint8_t* src = of.buf.data() + (size_t)j * of.bpp;
        of.fl->store(p, src);
      }
    }
    return;
  }

  if (of.fl->dest == DestKind::SoA) {
    copy_chunk_to_soa(out, *of.fl, ctx.outBase, ctx.nread, of.bpp, of.buf,
                      ctx.masked, ctx.keepPtr);
    return;
  }

  if (!ctx.masked) {
    for (size_t j = 0; j < ctx.nread; ++j) {
      const size_t oi = ctx.outBase + j;
      const uint8_t* src = of.buf.data() + j * of.bpp;
      switch (of.fl->spec.type) {
        case DataType::Float:  writeFieldToParticleBlock(out, oi, *of.fl, reinterpret_cast<const float*>(src)); break;
        case DataType::Double: writeFieldToParticleBlock(out, oi, *of.fl, reinterpret_cast<const double*>(src)); break;
        case DataType::Int32:  writeFieldToParticleBlock(out, oi, *of.fl, reinterpret_cast<const int32_t*>(src)); break;
        case DataType::Int64:  writeFieldToParticleBlock(out, oi, *of.fl, reinterpret_cast<const int64_t*>(src)); break;
      }
    }
  } else {
    for (size_t kk = 0; kk < ctx.keepPtr->size(); ++kk) {
      const uint32_t j = (*ctx.keepPtr)[kk];
      const size_t oi = ctx.outBase + kk;
      const uint8_t* src = of.buf.data() + (size_t)j * of.bpp;
      switch (of.fl->spec.type) {
        case DataType::Float:  writeFieldToParticleBlock(out, oi, *of.fl, reinterpret_cast<const float*>(src)); break;
        case DataType::Double: writeFieldToParticleBlock(out, oi, *of.fl, reinterpret_cast<const double*>(src)); break;
        case DataType::Int32:  writeFieldToParticleBlock(out, oi, *of.fl, reinterpret_cast<const int32_t*>(src)); break;
        case DataType::Int64:  writeFieldToParticleBlock(out, oi, *of.fl, reinterpret_cast<const int64_t*>(src)); break;
      }
    }
  }
}

void HDF5Reader::accumulate_temp_synth_inputs_(const OpenedField& of,
                                               const TempSynthAvailability& tempAvail,
                                               const ChunkContext& ctx,
                                               TempSynthBuffers& tmp)
{
  auto fill_chunk_scalar = [&](std::vector<double>& dst, DataType srcType)
  {
    auto get_double = [&](const uint8_t* p)->double {
      switch (srcType) {
        case DataType::Float:  { float   v; std::memcpy(&v, p, sizeof(v)); return (double)v; }
        case DataType::Double: { double  v; std::memcpy(&v, p, sizeof(v)); return v; }
        case DataType::Int32:  { int32_t v; std::memcpy(&v, p, sizeof(v)); return (double)v; }
        case DataType::Int64:  { int64_t v; std::memcpy(&v, p, sizeof(v)); return (double)v; }
      }
      return -1.0;
    };

    if (!ctx.masked) {
      for (size_t j = 0; j < ctx.nread; ++j) {
        dst[j] = get_double(of.buf.data() + j * of.elemSz);
      }
    } else {
      for (size_t kk = 0; kk < ctx.keepPtr->size(); ++kk) {
        const uint32_t j = (*ctx.keepPtr)[kk];
        dst[kk] = get_double(of.buf.data() + (size_t)j * of.elemSz);
      }
    }
  };

  if (of.fl->ftype == FieldKey::InternalEnergy) {
    fill_chunk_scalar(tmp.u, of.fl->spec.type);
  } else if (tempAvail.hasE && of.fl->ftype == FieldKey::ElectronAbundance) {
    fill_chunk_scalar(tmp.e, of.fl->spec.type);
  } else if (tempAvail.hasH2 && of.fl->ftype == FieldKey::H2Abundance) {
    fill_chunk_scalar(tmp.h2, of.fl->spec.type);
  } else if (tempAvail.hasG && of.fl->ftype == FieldKey::Gamma) {
    fill_chunk_scalar(tmp.g, of.fl->spec.type);
  }
}

HDF5Reader::TempSynthAvailability
HDF5Reader::probe_temp_synth_availability_(int ptype,
					   const BinaryReadLayout& layout,
					   const TempSynthRequest& req) const
{
  TempSynthAvailability avail;

  if (ptype != 0) return avail;

  auto findDSName = [&](FieldKey ft) -> std::string {
    for (const auto& fl : layout.fields) {
      if (fl.dest == DestKind::Ignore && !isTempSynthField(fl.ftype)) continue;
      if (fl.ftype != ft) continue;
      return fl.spec.sourceName;
    }
    return std::string();
  };

  auto tryOpen = [&](const std::string& dsName) -> bool {
    if (dsName.empty()) return false;
    try {
      (void)openDataSetWithDAPL(partPath(ptype, dsName));
      return true;
    } catch (...) {
      return false;
    }
  };

  if (req.wantTemp) avail.hasTemp = tryOpen(findDSName(FieldKey::Temperature));
  if (req.wantU)    avail.hasU    = tryOpen(findDSName(FieldKey::InternalEnergy));
  if (req.wantE)    avail.hasE    = tryOpen(findDSName(FieldKey::ElectronAbundance));
  if (req.wantH2)   avail.hasH2   = tryOpen(findDSName(FieldKey::H2Abundance));
  if (req.wantG)    avail.hasG    = tryOpen(findDSName(FieldKey::Gamma));

  avail.needSynth = (!avail.hasTemp && avail.hasU);
  return avail;
}

void HDF5Reader::synthesize_temperature_chunk_(ParticleBlock& out,
                                               const ChunkContext& ctx,
                                               const TempSynthAvailability& tempAvail,
                                               const TempSynthBuffers& tmp)
{
  for (size_t kk = 0; kk < ctx.nwrite; ++kk) {
    const double u = tmp.u[kk];
    if (!(u > 0.0)) continue;

    double gamma = 5.0 / 3.0;
    if (tempAvail.hasG && tmp.g[kk] > 0.0) gamma = tmp.g[kk];

    double denom = 1.2;
    if (tempAvail.hasE  && tmp.e[kk]  > 0.0) denom += tmp.e[kk];
    if (tempAvail.hasH2 && tmp.h2[kk] > 0.0) denom -= tmp.h2[kk];

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
  tmp.u.assign(nwrite, -1.0);
  tmp.e.clear();
  tmp.h2.clear();
  tmp.g.clear();

  if (tempAvail.hasE)  tmp.e.assign(nwrite, -1.0);
  if (tempAvail.hasH2) tmp.h2.assign(nwrite, -1.0);
  if (tempAvail.hasG)  tmp.g.assign(nwrite, -1.0);
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

void HDF5Reader::allocate_output_from_layout_(ParticleBlock& out,
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
    // fallback（最悪時）
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

void HDF5Reader::apply_density_scale_(ParticleBlock& out)
{
  if (factor_density_ == 1.0) return;
  
  for (auto& p : out.particles) {
    p.density = (float)((double)p.density * factor_density_);
  }
}

void HDF5Reader::apply_bfield_scale_(ParticleBlock& out)
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

void HDF5Reader::initialize_particle_defaults_chunk_(ParticleBlock& out,
						     int ptype,
						     size_t outBase,
						     size_t nwrite)
{
  for (size_t kk = 0; kk < nwrite; ++kk) {
    ParticleData& p = out.particles[outBase + kk];
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
 
