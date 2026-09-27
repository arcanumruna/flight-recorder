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
