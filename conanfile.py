from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMakeDeps, CMake, cmake_layout
import os


class FlightRecorderConan(ConanFile):
    name = "flight-recorder"
    package_type = "library"
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False], "flight_recorder": [True, False]}
    default_options = {"shared": False, "fPIC": True, "flight_recorder": True}
    exports_sources = "src/*", "include/*", "cmake/*", "CMakeLists.txt", "VERSION.txt"

    def set_version(self):
        version_file = os.path.join(os.path.dirname(__file__), "VERSION.txt")
        self.version = open(version_file).read().strip()

    def requirements(self):
        # flight-recorder's per-thread ring is built on aeron's header-only AtomicBuffer and its
        # RecordDescriptor framing. The aeron ConanCenter recipe raises
        # ConanInvalidConfiguration on macOS/armv8 (Apple Silicon), which would
        # break `conan install` + CMake configure for IDE loading on a Mac. Real
        # builds/tests run on Linux; on macOS skip aeron and let CMake use the
        # header-only fallback (FLIGHT_RECORDER_AERON_INCLUDE_DIR) or a configure-only stub.
        skip_aeron = (str(self.settings.os) == "Macos" and
                      str(self.settings.arch) == "armv8")
        for r in self.conan_data.get("requirements", []):
            if skip_aeron and r.startswith("aeron/"):
                continue
            self.requires(r)

    def build_requirements(self):
        for requirement in self.conan_data.get("test_requirements", []):
            self.test_requires(requirement)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.variables["FLIGHT_RECORDER"] = self.options.flight_recorder
        tc.generate()

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.set_property("cmake_target_name", "flight_recorder::flight_recorder")
        self.cpp_info.libs = ["flight_recorder_core"]
        self.cpp_info.includedirs = ["include"]
        if self.options.flight_recorder:
            self.cpp_info.defines = ["FLIGHT_RECORDER=1"]
        else:
            self.cpp_info.defines = ["FLIGHT_RECORDER=0"]
