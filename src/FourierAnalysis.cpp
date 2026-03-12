
#include "FourierAnalysis.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include "GridTypes.h"

namespace fourier_analysis {
  namespace {
    constexpr double kPi = 3.141592653589793238462643383279502884;

    int wrappedFrequency(int i, int n) {
      return (i <= n / 2) ? i : (i - n);
    }
  }  // namespace

  FourierGrid3D::FourierGrid3D() = default;

  FourierGrid3D::FourierGrid3D(const grid_analysis::Region3D& region, const grid_analysis::GridSize3D& size) {
    configure(region, size);
  }

  FourierGrid3D::~FourierGrid3D() {
    destroyPlans();
  }

  FourierGrid3D::FourierGrid3D(FourierGrid3D&& other) noexcept {
    *this = std::move(other);
  }

  FourierGrid3D& FourierGrid3D::operator=(FourierGrid3D&& other) noexcept {
    if (this == &other) return *this;

    destroyPlans();

    region_ = other.region_;
    size_ = other.size_;
    fieldKind_ = other.fieldKind_;

    scalar_ = std::move(other.scalar_);
    vx_ = std::move(other.vx_);
    vy_ = std::move(other.vy_);
    vz_ = std::move(other.vz_);

    scalarK_ = std::move(other.scalarK_);
    vkx_ = std::move(other.vkx_);
    vky_ = std::move(other.vky_);
    vkz_ = std::move(other.vkz_);

    realBuf1_ = std::move(other.realBuf1_);
    realBuf2_ = std::move(other.realBuf2_);
    realBuf3_ = std::move(other.realBuf3_);
    complexBuf1_ = std::move(other.complexBuf1_);
    complexBuf2_ = std::move(other.complexBuf2_);
    complexBuf3_ = std::move(other.complexBuf3_);

    planForward1_ = other.planForward1_;
    planForward2_ = other.planForward2_;
    planForward3_ = other.planForward3_;
    planBackward1_ = other.planBackward1_;
    planBackward2_ = other.planBackward2_;
    planBackward3_ = other.planBackward3_;

    other.planForward1_ = nullptr;
    other.planForward2_ = nullptr;
    other.planForward3_ = nullptr;
    other.planBackward1_ = nullptr;
    other.planBackward2_ = nullptr;
    other.planBackward3_ = nullptr;
    other.fieldKind_ = FieldKind::None;

    return *this;
  }

  void FourierGrid3D::setRegion(const grid_analysis::Region3D& region) {
    region_ = region;
  }

  void FourierGrid3D::setGridSize(const grid_analysis::GridSize3D& size) {
    size_ = size;
    allocateBuffers();
  }

  void FourierGrid3D::configure(const grid_analysis::Region3D& region, const grid_analysis::GridSize3D& size) {
    region_ = region;
    size_ = size;
    allocateBuffers();
  }

  void FourierGrid3D::clear() {
    fieldKind_ = FieldKind::None;
    scalar_.clear();
    vx_.clear();
    vy_.clear();
    vz_.clear();
    scalarK_.clear();
    vkx_.clear();
    vky_.clear();
    vkz_.clear();
  }

  void FourierGrid3D::setScalarField(const std::vector<double>& field) {
    checkConfigured();
    checkRealSize(field);
    fieldKind_ = FieldKind::Scalar;
    scalar_ = field;
    scalarK_.assign(complexSize(), {});
    vx_.clear();
    vy_.clear();
    vz_.clear();
    vkx_.clear();
    vky_.clear();
    vkz_.clear();
  }

  void FourierGrid3D::setVectorField(const std::vector<double>& fx,
				     const std::vector<double>& fy,
				     const std::vector<double>& fz) {
    checkConfigured();
    checkRealSize(fx);
    checkRealSize(fy);
    checkRealSize(fz);
    fieldKind_ = FieldKind::Vector3;
    vx_ = fx;
    vy_ = fy;
    vz_ = fz;
    vkx_.assign(complexSize(), {});
    vky_.assign(complexSize(), {});
    vkz_.assign(complexSize(), {});
    scalar_.clear();
    scalarK_.clear();
  }

  void FourierGrid3D::forwardScalarFFT() {
    if (fieldKind_ != FieldKind::Scalar) {
      throw std::runtime_error("FourierGrid3D: scalar field is not set.");
    }
    executeForward(scalar_, scalarK_, planForward1_, realBuf1_.get(), complexBuf1_.get());
  }

  void FourierGrid3D::forwardVectorFFT() {
    if (fieldKind_ != FieldKind::Vector3) {
      throw std::runtime_error("FourierGrid3D: vector field is not set.");
    }
    executeForward(vx_, vkx_, planForward1_, realBuf1_.get(), complexBuf1_.get());
    executeForward(vy_, vky_, planForward2_, realBuf2_.get(), complexBuf2_.get());
    executeForward(vz_, vkz_, planForward3_, realBuf3_.get(), complexBuf3_.get());
  }

  void FourierGrid3D::inverseScalarFFT(std::vector<double>& fieldOut) const {
    if (fieldKind_ != FieldKind::Scalar || scalarK_.empty()) {
      throw std::runtime_error("FourierGrid3D: scalar Fourier data is not available.");
    }
    executeInverse(scalarK_, fieldOut, planBackward1_, complexBuf1_.get(), realBuf1_.get());
  }

  void FourierGrid3D::inverseVectorFFT(std::vector<double>& fxOut,
				       std::vector<double>& fyOut,
				       std::vector<double>& fzOut) const {
    if (fieldKind_ != FieldKind::Vector3 || vkx_.empty() || vky_.empty() || vkz_.empty()) {
      throw std::runtime_error("FourierGrid3D: vector Fourier data is not available.");
    }
    executeInverse(vkx_, fxOut, planBackward1_, complexBuf1_.get(), realBuf1_.get());
    executeInverse(vky_, fyOut, planBackward2_, complexBuf2_.get(), realBuf2_.get());
    executeInverse(vkz_, fzOut, planBackward3_, complexBuf3_.get(), realBuf3_.get());
  }

  void FourierGrid3D::setScalarKSpace(const std::vector<std::complex<double>>& fk) {
    checkConfigured();
    checkComplexSize(fk);
    fieldKind_ = FieldKind::Scalar;
    scalarK_ = fk;
    scalar_.clear();
    vx_.clear();
    vy_.clear();
    vz_.clear();
    vkx_.clear();
    vky_.clear();
    vkz_.clear();
  }

  void FourierGrid3D::setVectorKSpace(const std::vector<std::complex<double>>& fkx,
				      const std::vector<std::complex<double>>& fky,
				      const std::vector<std::complex<double>>& fkz) {
    checkConfigured();
    checkComplexSize(fkx);
    checkComplexSize(fky);
    checkComplexSize(fkz);
    fieldKind_ = FieldKind::Vector3;
    vkx_ = fkx;
    vky_ = fky;
    vkz_ = fkz;
    scalar_.clear();
    scalarK_.clear();
    vx_.clear();
    vy_.clear();
    vz_.clear();
  }

  const std::vector<double>& FourierGrid3D::getScalarField() const { return scalar_; }
  const std::vector<double>& FourierGrid3D::getVectorFieldX() const { return vx_; }
  const std::vector<double>& FourierGrid3D::getVectorFieldY() const { return vy_; }
  const std::vector<double>& FourierGrid3D::getVectorFieldZ() const { return vz_; }

  const std::vector<std::complex<double>>& FourierGrid3D::getScalarKSpace() const { return scalarK_; }
  const std::vector<std::complex<double>>& FourierGrid3D::getVectorKSpaceX() const { return vkx_; }
  const std::vector<std::complex<double>>& FourierGrid3D::getVectorKSpaceY() const { return vky_; }
  const std::vector<std::complex<double>>& FourierGrid3D::getVectorKSpaceZ() const { return vkz_; }

  const grid_analysis::Region3D& FourierGrid3D::getRegion() const { return region_; }
  const grid_analysis::GridSize3D& FourierGrid3D::getGridSize() const { return size_; }
  FourierGrid3D::FieldKind FourierGrid3D::getFieldKind() const { return fieldKind_; }

  std::size_t FourierGrid3D::realSize() const { return size_.realSize(); }
  std::size_t FourierGrid3D::complexSize() const { return size_.complexSize(); }

  void FourierGrid3D::buildWaveVector(int ix, int iy, int iz, std::array<double, 3>& kvec) const {
    checkConfigured();
    const auto len = region_.lengths();
    const int kx = wrappedFrequency(ix, size_.nx);
    const int ky = wrappedFrequency(iy, size_.ny);
    const int kz = iz;

    kvec[0] = 2.0 * kPi * static_cast<double>(kx) / len[0];
    kvec[1] = 2.0 * kPi * static_cast<double>(ky) / len[1];
    kvec[2] = 2.0 * kPi * static_cast<double>(kz) / len[2];
  }

  void FourierGrid3D::FFTWDeleter::operator()(double* ptr) const noexcept {
    if (ptr != nullptr) fftw_free(ptr);
  }

  void FourierGrid3D::FFTWDeleter::operator()(fftw_complex* ptr) const noexcept {
    if (ptr != nullptr) fftw_free(ptr);
  }

  void FourierGrid3D::checkConfigured() const {
    if (!size_.valid()) {
      throw std::runtime_error("FourierGrid3D: grid size is not configured.");
    }
  }

  void FourierGrid3D::checkRealSize(const std::vector<double>& field) const {
    if (field.size() != realSize()) {
      throw std::runtime_error("FourierGrid3D: real-space field size mismatch.");
    }
  }

  void FourierGrid3D::checkComplexSize(const std::vector<std::complex<double>>& field) const {
    if (field.size() != complexSize()) {
      throw std::runtime_error("FourierGrid3D: Fourier-space field size mismatch.");
    }
  }

  void FourierGrid3D::allocateBuffers() {
    checkConfigured();
    destroyPlans();

    realBuf1_.reset(static_cast<double*>(fftw_malloc(sizeof(double) * realSize())));
    realBuf2_.reset(static_cast<double*>(fftw_malloc(sizeof(double) * realSize())));
    realBuf3_.reset(static_cast<double*>(fftw_malloc(sizeof(double) * realSize())));

    complexBuf1_.reset(static_cast<fftw_complex*>(fftw_malloc(sizeof(fftw_complex) * complexSize())));
    complexBuf2_.reset(static_cast<fftw_complex*>(fftw_malloc(sizeof(fftw_complex) * complexSize())));
    complexBuf3_.reset(static_cast<fftw_complex*>(fftw_malloc(sizeof(fftw_complex) * complexSize())));

    if (!realBuf1_ || !realBuf2_ || !realBuf3_ || !complexBuf1_ || !complexBuf2_ || !complexBuf3_) {
      throw std::runtime_error("FourierGrid3D: FFTW buffer allocation failed.");
    }

    planForward1_ = fftw_plan_dft_r2c_3d(size_.nx, size_.ny, size_.nz, realBuf1_.get(), complexBuf1_.get(), FFTW_MEASURE);
    planForward2_ = fftw_plan_dft_r2c_3d(size_.nx, size_.ny, size_.nz, realBuf2_.get(), complexBuf2_.get(), FFTW_MEASURE);
    planForward3_ = fftw_plan_dft_r2c_3d(size_.nx, size_.ny, size_.nz, realBuf3_.get(), complexBuf3_.get(), FFTW_MEASURE);

    planBackward1_ = fftw_plan_dft_c2r_3d(size_.nx, size_.ny, size_.nz, complexBuf1_.get(), realBuf1_.get(), FFTW_MEASURE);
    planBackward2_ = fftw_plan_dft_c2r_3d(size_.nx, size_.ny, size_.nz, complexBuf2_.get(), realBuf2_.get(), FFTW_MEASURE);
    planBackward3_ = fftw_plan_dft_c2r_3d(size_.nx, size_.ny, size_.nz, complexBuf3_.get(), realBuf3_.get(), FFTW_MEASURE);

    if (!planForward1_ || !planForward2_ || !planForward3_ ||
	!planBackward1_ || !planBackward2_ || !planBackward3_) {
      destroyPlans();
      throw std::runtime_error("FourierGrid3D: FFTW plan creation failed.");
    }
  }

  void FourierGrid3D::destroyPlans() {
    if (planForward1_) fftw_destroy_plan(planForward1_);
    if (planForward2_) fftw_destroy_plan(planForward2_);
    if (planForward3_) fftw_destroy_plan(planForward3_);
    if (planBackward1_) fftw_destroy_plan(planBackward1_);
    if (planBackward2_) fftw_destroy_plan(planBackward2_);
    if (planBackward3_) fftw_destroy_plan(planBackward3_);

    planForward1_ = nullptr;
    planForward2_ = nullptr;
    planForward3_ = nullptr;
    planBackward1_ = nullptr;
    planBackward2_ = nullptr;
    planBackward3_ = nullptr;
  }

  void FourierGrid3D::executeForward(const std::vector<double>& in,
				     std::vector<std::complex<double>>& out,
				     fftw_plan plan,
				     double* realBuf,
				     fftw_complex* complexBuf) const {
    std::memcpy(realBuf, in.data(), sizeof(double) * realSize());
    fftw_execute(plan);
    out.resize(complexSize());
    for (std::size_t i = 0; i < complexSize(); ++i) {
      out[i] = std::complex<double>(complexBuf[i][0], complexBuf[i][1]);
    }
  }

  void FourierGrid3D::executeInverse(const std::vector<std::complex<double>>& in,
				     std::vector<double>& out,
				     fftw_plan plan,
				     fftw_complex* complexBuf,
				     double* realBuf) const {
    for (std::size_t i = 0; i < complexSize(); ++i) {
      complexBuf[i][0] = in[i].real();
      complexBuf[i][1] = in[i].imag();
    }
    fftw_execute(plan);
    out.resize(realSize());
    const double norm = 1.0 / static_cast<double>(realSize());
    for (std::size_t i = 0; i < realSize(); ++i) {
      out[i] = realBuf[i] * norm;
    }
  }

  double PowerSpectrumAnalyzer::modePowerScalar(const std::complex<double>& v) {
    return std::norm(v);
  }

  double PowerSpectrumAnalyzer::modePowerVector(const std::complex<double>& vx,
						const std::complex<double>& vy,
						const std::complex<double>& vz) {
    return std::norm(vx) + std::norm(vy) + std::norm(vz);
  }

  void PowerSpectrumAnalyzer::decomposeMode(const std::array<std::complex<double>, 3>& vin,
					    const std::array<double, 3>& kvec,
					    std::array<std::complex<double>, 3>& vsol,
					    std::array<std::complex<double>, 3>& vcomp) {
    const double k2 = kvec[0] * kvec[0] + kvec[1] * kvec[1] + kvec[2] * kvec[2];
    if (k2 == 0.0) {
      vsol = vin;
      vcomp = {std::complex<double>(0.0, 0.0),
	       std::complex<double>(0.0, 0.0),
	       std::complex<double>(0.0, 0.0)};
      return;
    }

    const std::complex<double> kdotv =
      kvec[0] * vin[0] + kvec[1] * vin[1] + kvec[2] * vin[2];

    for (int c = 0; c < 3; ++c) {
      vcomp[c] = kvec[c] * kdotv / k2;
      vsol[c] = vin[c] - vcomp[c];
    }
  }

  int PowerSpectrumAnalyzer::shellIndex(double kmag, double dk) const {
    return static_cast<int>(std::floor(kmag / dk));
  }

  PowerSpectrumAnalyzer::ScalarSpectrum1D
  PowerSpectrumAnalyzer::computeScalarSpectrum(const FourierGrid3D& fourier) const {
    if (fourier.getFieldKind() != FourierGrid3D::FieldKind::Scalar) {
      throw std::runtime_error("PowerSpectrumAnalyzer: scalar Fourier data is required.");
    }

    const auto& size = fourier.getGridSize();
    const auto len = fourier.getRegion().lengths();
    const double dk =
      std::min({2.0 * kPi / len[0], 2.0 * kPi / len[1], 2.0 * kPi / len[2]});

    const int nbins = std::max({size.nx, size.ny, size.nz}) / 2 + 1;
    ScalarSpectrum1D spec;
    spec.kCenter.assign(nbins, 0.0);
    spec.power.assign(nbins, 0.0);
    spec.nModes.assign(nbins, 0);

    const auto& fk = fourier.getScalarKSpace();
    const int nzc = size.nz / 2 + 1;

    for (int ix = 0; ix < size.nx; ++ix) {
      for (int iy = 0; iy < size.ny; ++iy) {
	for (int iz = 0; iz < nzc; ++iz) {
	  const std::size_t idx =
            (static_cast<std::size_t>(ix) * size.ny + static_cast<std::size_t>(iy)) * nzc +
            static_cast<std::size_t>(iz);

	  std::array<double, 3> kvec{};
	  fourier.buildWaveVector(ix, iy, iz, kvec);
	  const double kmag = std::sqrt(kvec[0] * kvec[0] + kvec[1] * kvec[1] + kvec[2] * kvec[2]);

	  const int shell = shellIndex(kmag, dk);
	  if (shell < 0 || shell >= nbins) continue;

	  spec.kCenter[shell] += kmag;
	  spec.power[shell] += modePowerScalar(fk[idx]);
	  spec.nModes[shell] += 1;
	}
      }
    }

    for (int i = 0; i < nbins; ++i) {
      if (spec.nModes[i] > 0) {
	spec.kCenter[i] /= static_cast<double>(spec.nModes[i]);
	spec.power[i] /= static_cast<double>(spec.nModes[i]);
      }
    }

    return spec;
  }

  PowerSpectrumAnalyzer::VectorSpectrum1D
  PowerSpectrumAnalyzer::computeVectorSpectrum(const FourierGrid3D& fourier) const {
    if (fourier.getFieldKind() != FourierGrid3D::FieldKind::Vector3) {
      throw std::runtime_error("PowerSpectrumAnalyzer: vector Fourier data is required.");
    }

    const auto& size = fourier.getGridSize();
    const auto len = fourier.getRegion().lengths();
    const double dk =
      std::min({2.0 * kPi / len[0], 2.0 * kPi / len[1], 2.0 * kPi / len[2]});

    const int nbins = std::max({size.nx, size.ny, size.nz}) / 2 + 1;
    VectorSpectrum1D spec;
    spec.kCenter.assign(nbins, 0.0);
    spec.powerTotal.assign(nbins, 0.0);
    spec.powerSolenoidal.assign(nbins, 0.0);
    spec.powerCompressive.assign(nbins, 0.0);
    spec.nModes.assign(nbins, 0);

    const auto& fkx = fourier.getVectorKSpaceX();
    const auto& fky = fourier.getVectorKSpaceY();
    const auto& fkz = fourier.getVectorKSpaceZ();
    const int nzc = size.nz / 2 + 1;

    for (int ix = 0; ix < size.nx; ++ix) {
      for (int iy = 0; iy < size.ny; ++iy) {
	for (int iz = 0; iz < nzc; ++iz) {
	  const std::size_t idx =
            (static_cast<std::size_t>(ix) * size.ny + static_cast<std::size_t>(iy)) * nzc +
            static_cast<std::size_t>(iz);

	  std::array<double, 3> kvec{};
	  fourier.buildWaveVector(ix, iy, iz, kvec);
	  const double kmag = std::sqrt(kvec[0] * kvec[0] + kvec[1] * kvec[1] + kvec[2] * kvec[2]);

	  const int shell = shellIndex(kmag, dk);
	  if (shell < 0 || shell >= nbins) continue;

	  std::array<std::complex<double>, 3> vin{fkx[idx], fky[idx], fkz[idx]};
	  std::array<std::complex<double>, 3> vsol{};
	  std::array<std::complex<double>, 3> vcomp{};
	  decomposeMode(vin, kvec, vsol, vcomp);

	  spec.kCenter[shell] += kmag;
	  spec.powerTotal[shell] += modePowerVector(vin[0], vin[1], vin[2]);
	  spec.powerSolenoidal[shell] += modePowerVector(vsol[0], vsol[1], vsol[2]);
	  spec.powerCompressive[shell] += modePowerVector(vcomp[0], vcomp[1], vcomp[2]);
	  spec.nModes[shell] += 1;
	}
      }
    }

    for (int i = 0; i < nbins; ++i) {
      if (spec.nModes[i] > 0) {
	const double inv = 1.0 / static_cast<double>(spec.nModes[i]);
	spec.kCenter[i] *= inv;
	spec.powerTotal[i] *= inv;
	spec.powerSolenoidal[i] *= inv;
	spec.powerCompressive[i] *= inv;
      }
    }

    return spec;
  }

  void HelmholtzDecomposer::decomposeMode(const std::array<std::complex<double>, 3>& vin,
					  const std::array<double, 3>& kvec,
					  std::array<std::complex<double>, 3>& vsol,
					  std::array<std::complex<double>, 3>& vcomp) {
    const double k2 = kvec[0] * kvec[0] + kvec[1] * kvec[1] + kvec[2] * kvec[2];
    if (k2 == 0.0) {
      vsol = vin;
      vcomp = {std::complex<double>(0.0, 0.0),
	       std::complex<double>(0.0, 0.0),
	       std::complex<double>(0.0, 0.0)};
      return;
    }

    const std::complex<double> kdotv =
      kvec[0] * vin[0] + kvec[1] * vin[1] + kvec[2] * vin[2];

    for (int c = 0; c < 3; ++c) {
      vcomp[c] = kvec[c] * kdotv / k2;
      vsol[c] = vin[c] - vcomp[c];
    }
  }

  HelmholtzDecomposer::DecompositionResult
  HelmholtzDecomposer::decompose(const FourierGrid3D& fourier) const {
    if (fourier.getFieldKind() != FourierGrid3D::FieldKind::Vector3) {
      throw std::runtime_error("HelmholtzDecomposer: vector Fourier data is required.");
    }

    FourierGrid3D solGrid(fourier.getRegion(), fourier.getGridSize());
    FourierGrid3D compGrid(fourier.getRegion(), fourier.getGridSize());

    const auto& fkx = fourier.getVectorKSpaceX();
    const auto& fky = fourier.getVectorKSpaceY();
    const auto& fkz = fourier.getVectorKSpaceZ();

    std::vector<std::complex<double>> fkxSol(fkx.size());
    std::vector<std::complex<double>> fkySol(fky.size());
    std::vector<std::complex<double>> fkzSol(fkz.size());
    std::vector<std::complex<double>> fkxComp(fkx.size());
    std::vector<std::complex<double>> fkyComp(fky.size());
    std::vector<std::complex<double>> fkzComp(fkz.size());

    const auto& size = fourier.getGridSize();
    const int nzc = size.nz / 2 + 1;

    for (int ix = 0; ix < size.nx; ++ix) {
      for (int iy = 0; iy < size.ny; ++iy) {
	for (int iz = 0; iz < nzc; ++iz) {
	  const std::size_t idx =
            (static_cast<std::size_t>(ix) * size.ny + static_cast<std::size_t>(iy)) * nzc +
            static_cast<std::size_t>(iz);

	  std::array<double, 3> kvec{};
	  fourier.buildWaveVector(ix, iy, iz, kvec);

	  std::array<std::complex<double>, 3> vin{fkx[idx], fky[idx], fkz[idx]};
	  std::array<std::complex<double>, 3> vsol{};
	  std::array<std::complex<double>, 3> vcomp{};
	  decomposeMode(vin, kvec, vsol, vcomp);

	  fkxSol[idx] = vsol[0];
	  fkySol[idx] = vsol[1];
	  fkzSol[idx] = vsol[2];

	  fkxComp[idx] = vcomp[0];
	  fkyComp[idx] = vcomp[1];
	  fkzComp[idx] = vcomp[2];
	}
      }
    }

    solGrid.setVectorKSpace(fkxSol, fkySol, fkzSol);
    compGrid.setVectorKSpace(fkxComp, fkyComp, fkzComp);

    DecompositionResult result;
    solGrid.inverseVectorFFT(result.solenoidal.x, result.solenoidal.y, result.solenoidal.z);
    compGrid.inverseVectorFFT(result.compressive.x, result.compressive.y, result.compressive.z);

    result.solenoidalFourier = std::move(solGrid);
    result.compressiveFourier = std::move(compGrid);
    return result;
  }
}  // namespace fourier_analysis
