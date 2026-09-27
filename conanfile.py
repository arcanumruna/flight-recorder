from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMakeDeps, CMake, cmake_layout
import os


class RuneFrConan(ConanFile):
    name = "rune-fr"
    package_type = "library"
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False], "rune_fr": [True, False]}
    default_options = {"shared": False, "fPIC": True, "rune_fr": True}
    exports_sources = "src/*", "include/*", "cmake/*", "CMakeLists.txt", "VERSION.txt"

    def set_version(self):
        version_file = os.path.join(os.path.dirname(__file__), "VERSION.txt")
        self.version = open(version_file).read().strip()

    def requirements(self):
        # rune-fr's per-thread ring is built on aeron's header-only AtomicBuffer/
        # RecordDescriptor. The aeron/1.41.4 ConanCenter recipe raises
        # ConanInvalidConfiguration on macOS/armv8 (Apple Silicon), which would
        # break `conan install` + CMake configure for IDE loading on a Mac. Real
        # builds/tests run on Linux; on macOS skip aeron and let CMake use the
        # header-only fallback (RUNE_FR_AERON_INCLUDE_DIR) or a configure-only stub.
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
        tc.variables["RUNE_FR"] = self.options.rune_fr
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
        self.cpp_info.set_property("cmake_target_name", "rune_fr::rune_fr")
        self.cpp_info.libs = ["rune_fr_core"]
        self.cpp_info.includedirs = ["include"]
        if self.options.rune_fr:
            self.cpp_info.defines = ["RUNE_FR=1"]
        else:
            self.cpp_info.defines = ["RUNE_FR=0"]
