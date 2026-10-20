Import("env")

cmake_dir = env.PioPlatform().get_package_dir("tool-cmake")
if cmake_dir:
    env.PrependENVPath("PATH", f"{cmake_dir}/bin")
