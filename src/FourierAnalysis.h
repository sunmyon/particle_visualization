
#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <vector>

#include <fftw3.h>

#include "GridTypes.h"

namespace fourier_analysis {
  class FourierGrid3D {
  public:
    enum class FieldKind {
      None,
      Scalar,
      Vector3
    };

    FourierGrid3D();
    FourierGrid3D(const grid_analysis::Region3D& region,
                  const grid_analysis::GridSize3D& size);
    ~FourierGrid3D();

    FourierGrid3D(const FourierGrid3D&) = delete;
    FourierGrid3D& operator=(const FourierGrid3D&) = delete;
    FourierGrid3D(FourierGrid3D&& other) noexcept;
    FourierGrid3D& operator=(FourierGrid3D&& other) noexcept;

    void setRegion(const grid_analysis::Region3D& region);
    void setGridSize(const grid_analysis::GridSize3D& size);
    void configure(const grid_analysis::Region3D& region,
                   const grid_analysis::GridSize3D& size);
    void clear();

    void setScalarField(const std::vector<double>& field);
    void setVectorField(const std::vector<double>& fx,
			const std::vector<double>& fy,
			const std::vector<double>& fz);

    void forwardScalarFFT();
    void forwardVectorFFT();

    void inverseScalarFFT(std::vector<double>& fieldOut) const;
    void inverseVectorFFT(std::vector<double>& fxOut,
			  std::vector<double>& fyOut,
			  std::vector<double>& fzOut) const;

    void setScalarKSpace(const std::vector<std::complex<double>>& fk);
    void setVectorKSpace(const std::vector<std::complex<double>>& fkx,
			 const std::vector<std::complex<double>>& fky,
			 const std::vector<std::complex<double>>& fkz);

    [[nodiscard]] const std::vector<double>& getScalarField() const;
    [[nodiscard]] const std::vector<double>& getVectorFieldX() const;
    [[nodiscard]] const std::vector<double>& getVectorFieldY() const;
    [[nodiscard]] const std::vector<double>& getVectorFieldZ() const;

    [[nodiscard]] const std::vector<std::complex<double>>& getScalarKSpace() const;
    [[nodiscard]] const std::vector<std::complex<double>>& getVectorKSpaceX() const;
    [[nodiscard]] const std::vector<std::complex<double>>& getVectorKSpaceY() const;
    [[nodiscard]] const std::vector<std::complex<double>>& getVectorKSpaceZ() const;

    [[nodiscard]] const grid_analysis::Region3D& getRegion() const;
    [[nodiscard]] const grid_analysis::GridSize3D& getGridSize() const;
    [[nodiscard]] FieldKind getFieldKind() const;

    [[nodiscard]] std::size_t realSize() const;
    [[nodiscard]] std::size_t complexSize() const;

    void buildWaveVector(int ix, int iy, int iz, std::array<double, 3>& kvec) const;

  private:
    struct FFTWDeleter {
      void operator()(double* ptr) const noexcept;
      void operator()(fftw_complex* ptr) const noexcept;
    };

    using RealPtr = std::unique_ptr<double, FFTWDeleter>;
    using ComplexPtr = std::unique_ptr<fftw_complex, FFTWDeleter>;

    void checkConfigured() const;
    void checkRealSize(const std::vector<double>& field) const;
    void checkComplexSize(const std::vector<std::complex<double>>& field) const;

    void allocateBuffers();
    void destroyPlans();

    void executeForward(const std::vector<double>& in,
			std::vector<std::complex<double>>& out,
			fftw_plan plan,
			double* realBuf,
			fftw_complex* complexBuf) const;

    void executeInverse(const std::vector<std::complex<double>>& in,
			std::vector<double>& out,
			fftw_plan plan,
			fftw_complex* complexBuf,
			double* realBuf) const;

    grid_analysis::Region3D region_{};
    grid_analysis::GridSize3D size_{};
    FieldKind fieldKind_ = FieldKind::None;

    std::vector<double> scalar_;
    std::vector<double> vx_;
    std::vector<double> vy_;
    std::vector<double> vz_;

    std::vector<std::complex<double>> scalarK_;
    std::vector<std::complex<double>> vkx_;
    std::vector<std::complex<double>> vky_;
    std::vector<std::complex<double>> vkz_;

    RealPtr realBuf1_;
    RealPtr realBuf2_;
    RealPtr realBuf3_;
    ComplexPtr complexBuf1_;
    ComplexPtr complexBuf2_;
    ComplexPtr complexBuf3_;

    fftw_plan planForward1_ = nullptr;
    fftw_plan planForward2_ = nullptr;
    fftw_plan planForward3_ = nullptr;
    fftw_plan planBackward1_ = nullptr;
    fftw_plan planBackward2_ = nullptr;
    fftw_plan planBackward3_ = nullptr;
  };

  class PowerSpectrumAnalyzer {
  public:
    struct ScalarSpectrum1D {
      std::vector<double> kCenter;
      std::vector<double> power;
      std::vector<int> nModes;
    };

    struct VectorSpectrum1D {
      std::vector<double> kCenter;
      std::vector<double> powerTotal;
      std::vector<double> powerSolenoidal;
      std::vector<double> powerCompressive;
      std::vector<int> nModes;
    };

    [[nodiscard]] ScalarSpectrum1D computeScalarSpectrum(const FourierGrid3D& fourier) const;
    [[nodiscard]] VectorSpectrum1D computeVectorSpectrum(const FourierGrid3D& fourier) const;

  private:
    static double modePowerScalar(const std::complex<double>& v);
    static double modePowerVector(const std::complex<double>& vx,
				  const std::complex<double>& vy,
				  const std::complex<double>& vz);

    static void decomposeMode(const std::array<std::complex<double>, 3>& vin,
			      const std::array<double, 3>& kvec,
			      std::array<std::complex<double>, 3>& vsol,
			      std::array<std::complex<double>, 3>& vcomp);

    int shellIndex(double kmag, double dk) const;
  };

  class HelmholtzDecomposer {
  public:
    struct VectorField3D {
      std::vector<double> x;
      std::vector<double> y;
      std::vector<double> z;
    };

    struct DecompositionResult {
      VectorField3D solenoidal;
      VectorField3D compressive;
      FourierGrid3D solenoidalFourier;
      FourierGrid3D compressiveFourier;
    };

    [[nodiscard]] DecompositionResult decompose(const FourierGrid3D& fourier) const;

  private:
    static void decomposeMode(const std::array<std::complex<double>, 3>& vin,
			      const std::array<double, 3>& kvec,
			      std::array<std::complex<double>, 3>& vsol,
			      std::array<std::complex<double>, 3>& vcomp);
  };

}  // namespace fourier_analysis
