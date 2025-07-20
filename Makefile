# Makefile for 3D Particle Visualization with GLFW, GLAD, GLM, and ImGui

# コンパイラ
CC = gcc
CXX = g++

SRC_DIR := src
BUILD_DIR := build
EXTERNAL_DIR := external

################## OPTIONS ####################
OPTION += -DDEBUG_MODE

#OPTION += -DUSE_LUA
#OPTION += -DUSE_CONVEX_HULL
#OPTION += -DCLUMP_DATA_READ
#OPTION += -DISO_CONTOUR
#OPTION += -DNONATIVEFILEDIALOG
#OPTION += -DGEOMETRICAL_ANALYSIS
#OPTION += -DUSE_TBB
OPTION += -DSTREAM_LINE
OPTION += -DHAVE_HDF5
OPTION += -DUSE_MMAP

OPTION += -DSAVE_GPU_MEMORY
OPTION += -DROTATE_QUATERNION
OPTION += -DROTATE_ARCBALL
################################################

UNAME_S := $(shell uname -s)
ARCH := $(shell uname -m)

LDFLAGS := -lglfw -lgmp -lmpfr

ifeq ($(findstring HAVE_HDF5, $(OPTION)), HAVE_HDF5)
LDFLAGS += -lhdf5_cpp -lhdf5
endif

ifeq ($(findstring USE_LUA, $(OPTION)), USE_LUA)
LDFLAGS += -llua
endif

ifeq ($(findstring USE_TBB, $(OPTION)), USE_TBB)
LDFLAGS += -ltbb
endif

ifeq ($(UNAME_S), Darwin)
NFD_SRC_M = nfd/nfd_cocoa.m
NFD_LIBS = -framework Cocoa

OPENMP_CFLAGS = -Xpreprocessor -fopenmp
OPENMP_LDFLAGS = -lomp

OPTION += -DMACOS
ifeq ($(ARCH),arm64)
INCL = -I${HOME}/usr/local/include/vtk-9.4 -I/opt/homebrew/include  -I/opt/homebrew/opt/libomp/include
LIBS = -L${HOME}/usr/local/lib -L/opt/homebrew/lib -L/opt/homebrew/opt/libomp/lib
EIGEN_INC := /opt/homebrew/include/eigen3
else
INCL = -I${HOME}/usr/local/include/vtk-9.4  -I/usr/local/include -I/usr/local/opt/libomp/include
LIBS = -L${HOME}/usr/local/lib -L/usr/local/lib -L/usr/local/opt/libomp/lib
EIGEN_INC := /usr/local/include/eigen3
endif

LDFLAGS += -framework OpenGL -framework Cocoa
else
LDFLAGS += -ldl -lGL -lpthread
endif

ifeq ($(UNAME_S),Linux)
    # Linuxの場合: GTK+ を使用するなら nfd_gtk.c を使う（Zenity版 nfd_zenity.c も選択可）
    #NFD_SRC_C = nfd_zenity.c	
    # GTK+ の場合、pkg-configで必要なフラグを取得
    #NFD_LIBS = $(shell pkg-config --cflags --libs gtk+-3.0)
INCL = -I${HOME}/usr/local/include
LIBS = -L${HOME}/usr/local/lib64 -L${HOME}/usr/local/lib
OPTION += -DNONATIVEFILEDIALOG
OPENMP_CFLAGS = -fopenmp
endif

ifeq ($(UNAME_S),Windows_NT)
NFD_SRC_CPP = nfd/nfd_win.cpp
NFD_LIBS =
endif

INCL += -I$(SRC_DIR) -I$(EXTERNAL_DIR)/imgui -I$(EXTERNAL_DIR)/imgui/backends -I$(EXTERNAL_DIR)/glad -I$(EXTERNAL_DIR)/glad/include/ -I$(EXTERNAL_DIR)/include/ -I$(EXTERNAL_DIR)/implot -I$(EXTERNAL_DIR)/nfd

# コンパイルオプション（必要なインクルードディレクトリを指定）
CFLAGS = -std=c11 -g -Wall $(INCL) $(OPENMP_CFLAGS) $(OPTION)
CXXFLAGS = -std=c++17 -g -Wall $(INCL) -I$(EIGEN_INC) $(OPENMP_CFLAGS) $(OPTION)
OBJCFLAGS = -g -Wall $(INCL) $(OPTION)
LDFLAGS += $(LIBS) $(OPENMP_LDFLAGS)

ifeq ($(findstring DEBUG_MODE, $(OPTION)), DEBUG_MODE)
CFLAGS += -O0
CXXFLAGS += -O0 -fsanitize=address -fno-omit-frame-pointer
OBJCFLAGS += -O0
else
CFLAGS += -O2
CXXFLAGS += -O2
OBJCFLAGS += -O2
endif

# ソースファイル群
SRC_CPP_BASE = main.cpp make_2D_projection_map.cpp compute_2D_histogram.cpp compute_radial_profile.cpp 
SRC_CPP_BASE += FileIO/file_io.cpp FileIO/file_io_hdf5.cpp
SRC_CPP_BASE += FindClumps/find_clumps.cpp FindClumps/find_clumps_IO.cpp FindClumps/find_clumps_make_chains.cpp

ifeq ($(findstring GEOMETRICAL_ANALYSIS, $(OPTION)), GEOMETRICAL_ANALYSIS)
    SRC_CPP_BASE += GeometricAnalysis/ellipse_fitter.cpp
endif

ifeq ($(findstring USE_CONVEX_HULL, $(OPTION)), USE_CONVEX_HULL)
    SRC_CPP_BASE += FindClumps/create_convex_hull.cpp
endif

ifeq ($(findstring STREAM_LINE, $(OPTION)), STREAM_LINE)
    SRC_CPP_BASE += StreamLine/stream_line.cpp
endif

IMGUI_CPP_BASE = imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_widgets.cpp imgui/imgui_demo.cpp imgui/imgui_tables.cpp\
                 imgui/backends/imgui_impl_glfw.cpp imgui/backends/imgui_impl_opengl3.cpp

IMPLOT_CPP_BASE = implot/implot.cpp implot/implot_items.cpp


ifeq ($(findstring ISO_CONTOUR, $(OPTION)), ISO_CONTOUR)
#    VTK_ROOT = /opt/homebrew/opt/vtk
    VTK_ROOT = /Users/j/usr/local
    VTK_INCLUDE := $(VTK_ROOT)/include/vtk-9.4
    VTK_LIB     := $(VTK_ROOT)/lib

    # --- VTK モジュール名 & バージョン ---
    VTK_VER    := 9.4       # Homebrew の VTK バージョンに合わせてください
    VTK_MODULES := \
      CommonCore \
      CommonDataModel \
      CommonExecutionModel \
      FiltersCore \
      FiltersExtraction \
      FiltersGeneral \
      FiltersAMR \
      FiltersHyperTree \
      FiltersHybrid  \
      FiltersSources \
      FiltersModeling \
      CommonMisc 

    # --- VTK フラグ ---
    VTK_CXXFLAGS := -I$(VTK_INCLUDE)
    # ライブラリ名は例えば -lvtkCommonCore-9.2 のようにバージョン付きになることに注意
    VTK_LIBS := -L$(VTK_LIB) $(foreach m,$(VTK_MODULES),-lvtk$(m)-$(VTK_VER)) -lvtksys-$(VTK_VER)

    INCL += -I$(SRC_DIR)/IsoSurface
    SRC_CPP_BASE += IsoSurface/iso_contour_self.cpp IsoSurface/marching_cubes.cpp IsoSurface/connectivity_test.cpp IsoSurface/density_evaluate.cpp IsoSurface/IsoSurfaceGenerator.cpp

    CXXFLAGS += $(VTK_CXXFLAGS)
    LIBS += $(VTK_LIBS)
    LDFLAGS += $(VTK_LIBS) -Wl,-rpath,/Users/j/usr/local/lib
endif

NFD_SRC_C = nfd/nfd_common.c
ifeq ($(findstring NONATIVEFILEDIALOG, $(OPTION)), NONATIVEFILEDIALOG)
    IMGUI_CPP_BASE += ImGuiFileDialog/ImGuiFileDialog.cpp
    INCL += -I$(EXTERNAL_DIR)/ImGuiFileDialog/
    NFD_SRC_C =
    NFD_SRC_M =
    NFD_SRC_CPP =
    NFD_SRC_LIBS =
endif

SOURCES_CPP = $(addprefix $(SRC_DIR)/,$(SRC_CPP_BASE))\
              $(addprefix $(EXTERNAL_DIR)/,$(IMGUI_CPP_BASE))\
              $(addprefix $(EXTERNAL_DIR)/,$(IMPLOT_CPP_BASE))\
              $(addprefix $(EXTERNAL_DIR)/,$(NFD_SRC_CPP))

SRC_C_EXT_BASE = glad/src/glad.c $(NFD_SRC_C)
SOURCES_C = $(addprefix $(EXTERNAL_DIR)/,$(SRC_C_EXT_BASE))
SOURCES_M = $(addprefix $(EXTERNAL_DIR)/,$(NFD_SRC_M))

# ソースファイルからオブジェクトファイル名を生成
SOURCES = $(SOURCES_CPP) $(SOURCES_C) $(SOURCES_M)

OBJECTS_CPP := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(SOURCES_CPP))
OBJECTS_C   := $(patsubst %.c,  $(BUILD_DIR)/%.o,$(SOURCES_C))
OBJECTS_M   := $(patsubst %.m,  $(BUILD_DIR)/%.o,$(SOURCES_M))

OBJECTS = $(OBJECTS_CPP) $(OBJECTS_C) $(OBJECTS_M)

# 出力実行ファイル名
TARGET = particle_vis

# デフォルトターゲット
all: $(TARGET)

# リンク
$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# C++ ソースのコンパイルルール
$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# C ファイル
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC)  $(CFLAGS)  -c $< -o $@

# Objective-C / ObjC++
$(BUILD_DIR)/%.o: %.m
	@mkdir -p $(dir $@)
	$(CXX) $(OBJCFLAGS) -x objective-c -c $< -o $@

# クリーンアップ
clean:
	rm -f $(TARGET) $(OBJECTS)

OPTION_ADD += -DNONATIVEFILEDIALOG
OPTION_REMOVE += -DUSE_LUA
OPTION_REMOVE += -DUSE_CONVEX_HULL
OPTION_REMOVE += -DCLUMP_DATA_READ
OPTION_REMOVE += -DHAVE_HDF5
OPTION_REMOVE += -DUSE_MMAP

EMCC       := emcc
EMXX       := em++
EM_INCL    := $(INCL)
EM_REMOVE_OPTION  := $(OPTION_REMOVE)
EM_ADDITIONAL_DEFS := $(OPTION_ADD)

EM_REMOVE_LIBS    := -lhdf5_cpp -lhdf5 -llua -lgmp  -lgmp -lmpfr -framework Cocoa

EMFLAGS   := -std=c++17 -O0 -g4 -Wall $(EM_INCL) $(EM_OPTION) $(EM_ADDITIONAL_DEFS) \
              -s USE_GLFW=3 -s USE_WEBGL2=1 -s FULL_ES3=1 \
              -s ALLOW_MEMORY_GROWTH=1 -s ASSERTIONS=1

# たとえば HDF5/LUA をリンクしない代わりに、Web 上で読み込む静的アセットをプリロードしたいような場合は
EM_LDFLAGS := -s WASM=1 \
              -o particle_vis.html \
              --preload-file assets@/assets \
              -s "EXPORTED_FUNCTIONS=['_main']" \
              -s "EXPORTED_RUNTIME_METHODS=['ccall','cwrap']"

# 4.2. ソース→Emscripten用オブジェクトに変換
EM_SOURCES_CPP := $(SOURCES_CPP)
EM_SOURCES_C   := $(SOURCES_C)

EM_OBJECTS_CPP := $(EM_SOURCES_CPP:.cpp=.em.o)
EM_OBJECTS_C   := $(EM_SOURCES_C:.c=.em.o)
EM_OBJECTS     := $(EM_OBJECTS_CPP) $(EM_OBJECTS_C)

emcc: $(OBJECTS)
	@echo "=== Emscripten ビルドを開始 ==="
	$(EMXX) \
	  $(filter-out $(OPTION_REMOVE),$(CXXFLAGS)) \
	  $(EMFLAGS) \
	  $(filter-out $(EM_REMOVE_LIBS),$(LDFLAGS)) \
	  $(EM_OBJECTS) \
	  -o $(TARGET).html
	@echo "=== Emscripten ビルド 完了: $(TARGET).html ==="

clean_em:
	rm -f $(EM_OBJECTS) $(TARGET).html $(TARGET).js $(TARGET).wasm
