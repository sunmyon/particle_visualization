#pragma once

#ifdef HAVE_HDF5
#include <H5Cpp.h>
#include "hdf5_utils.h"

#include <array>
#include <vector>
#include <iostream>
#include <cstdint>
#include <string>

#include "FileIO/element_reader.h"
#include "FileIO/file_format_types.h"
#include "FileIO/hdf5_utils.h"

class HDF5Reader final : public IElementReader {
  H5::H5File file_;

  size_t npart_ = 0;
  size_t blockSize_ = 1 << 16; // chunk read
  double mass_type_[6]{};   // MassTable
  size_t count_[6]{};       // NumPart_Total/ThisFile
  size_t IndexStart_[7]{};  // prefix sum

  bool   flag_skip_DM_ = false;
  
public:
  HDF5Reader() = default;

  bool is_binary() override { return false; }
  size_t elementCount() const override { return npart_; }

  bool open(const std::string& path, HeaderInfo& header) override;
  void close() override;

  bool readRange(SimulationBlock& out,
		 size_t begin, size_t count,
		 const std::vector<FieldSpec>& fields,
		 ParticleMask* mask = nullptr) override;
  
private:
  double factor_density_ = 1.0;
  double factor_Bfield_ = 1.0;
  double factor_IntEnergy_ = 1.0;
  hid_t dapl_;
  
  H5::DataSet openDataSetWithDAPL(const std::string& fullPath) const;  
  bool finalize_layout_from_hdf5_(BinaryReadLayout& layout);
  void allocate_output_from_layout_(SimulationBlock& out,
				    const BinaryReadLayout& layout,
				    size_t totalCount);
  struct TempSynthRequest {
    bool wantTemp = false;
    bool wantU    = false;
    bool wantE    = false;
    bool wantH2   = false;
    bool wantG    = false;
  };

  struct TempSynthAvailability {
    bool hasTemp   = false;
    bool hasU      = false;
    bool hasE      = false;
    bool hasH2     = false;
    bool hasG      = false;
    bool needSynth = false;
  };

  struct TempSynthBuffers {
    std::vector<double> u;
    std::vector<double> e;
    std::vector<double> h2;
    std::vector<double> g;
  };

  struct OpenedField {
    FieldLayout* fl = nullptr;
    H5::DataSet ds;
    size_t elemSz = 0;
    int comps = 0;
    size_t bpp = 0;
    std::vector<uint8_t> buf;
  };

  struct ChunkContext {
    int ptype = 0;
    size_t localStart = 0;
    size_t done = 0;
    size_t nread = 0;

    size_t outStart = 0;
    size_t outBase = 0;
    size_t nwrite = 0;

    bool masked = false;
    const std::vector<uint32_t>* keepPtr = nullptr;
  };

  void init_temp_synth_buffers_(TempSynthBuffers& tmp,
				const TempSynthAvailability& tempAvail,
				size_t nwrite);

  TempSynthRequest build_temp_synth_request_(const BinaryReadLayout& layout) const;
  TempSynthAvailability probe_temp_synth_availability_(int ptype,
						       const BinaryReadLayout& layout,
						       const TempSynthRequest& req) const;

  bool build_opened_fields_for_ptype_(int ptype,
				      BinaryReadLayout& layout,
				      std::vector<OpenedField>& opened);

  bool prepare_chunk_context_(ChunkContext& ctx,
			      ParticleMask* mask,
			      std::vector<uint32_t>& keep,
			      size_t& outWriteCursor);
  
  void dispatch_opened_field_chunk_(SimulationBlock& out,
				    const OpenedField& of,
				    const ChunkContext& ctx);

  void accumulate_temp_synth_inputs_(const OpenedField& of,
				     const TempSynthAvailability& tempAvail,
				     const ChunkContext& ctx,
				     TempSynthBuffers& tmp);

  void synthesize_temperature_chunk_(SimulationBlock& out,
				     const ChunkContext& ctx,
				     const TempSynthAvailability& tempAvail,
				     const TempSynthBuffers& tmp);

  void apply_density_scale_(SimulationBlock& out);
  void apply_bfield_scale_(SimulationBlock& out);
  void initialize_particle_defaults_chunk_(SimulationBlock& out,
					   int ptype,
					   size_t outBase,
					   size_t nwrite);

  bool read_coords_chunk_(int ptype, size_t localStart, size_t n,
			  std::vector<uint8_t>& buf);
  
  bool read_ids_chunk_u64_(int ptype, size_t localStart, size_t n,
			   std::vector<uint64_t>& ids);
  
  bool build_keep_chunk_(int ptype, size_t localStart, size_t n,
			 ParticleMask& mask,
			 std::vector<uint32_t>& keep);
  
  bool prepareMaskedOutput_(size_t begin, size_t count,
			    ParticleMask& mask,
			    size_t& totalKept);

  static bool isTempSynthField(FieldKey ft);
};

#endif
