import os
import shutil
import subprocess
from pathlib import Path

# -----------------------------
# Configuration
# -----------------------------
SOURCE_FILE = "main.cpp"
OUTPUT_DIR = Path("release")
SHADER_DIR = Path("shader_source")
ASSET_DIR = Path("assets")

DLLS_TO_COPY = [
    r"C:\WINDOWS\system32\SDL3.dll"
]

CL_FLAGS = [
    "/O2",          # Full optimization
    "/DNDEBUG",     # Disable asserts
    "/DRELEASE",    # Define RELEASE
    "/EHsc",        # C++ exceptions    
    "/MD",          # Release runtime library
    "/nologo",      # No MSVC banner
    "/I", "src", 
    "/I", "src/generated",
    "/Fe" + str(OUTPUT_DIR / "pong_dx12.exe")
]

# -----------------------------
# Build function
# -----------------------------
def build():
    print("=== Building main.cpp in Release mode ===")

    # Ensure release folder exists
    OUTPUT_DIR.mkdir(exist_ok=True)

    # Run cl.exe
    cmd = ["cl.exe"] + CL_FLAGS + [SOURCE_FILE]
    print("Running:", " ".join(cmd))

    result = subprocess.run(cmd, shell=True)

    if result.returncode != 0:
        print("❌ Build failed")
        return
    print("✔ Build succeeded")

    # -----------------------------
    # Copy shader folder
    # -----------------------------
    if SHADER_DIR.exists():
        dest = OUTPUT_DIR / SHADER_DIR.name
        if dest.exists():
            shutil.rmtree(dest)
        shutil.copytree(SHADER_DIR, dest)
        print(f"✔ Copied shaders → {dest}")
    else:
        print("⚠ shader_source folder not found")
        
    # -----------------------------
    # Copy assets folder
    # -----------------------------
    if ASSET_DIR.exists():
        dest = OUTPUT_DIR / ASSET_DIR.name
        if dest.exists():
            shutil.rmtree(dest)
        shutil.copytree(ASSET_DIR, dest)
        print(f"✔ Copied assets → {dest}")
    else:
        print("⚠ assets folder not found")

    # -----------------------------
    # Copy required DLLs
    # -----------------------------
    for dll in DLLS_TO_COPY:
        src = Path(dll)
        if src.exists():
            shutil.copy(src, OUTPUT_DIR)
            print(f"✔ Copied DLL → {src.name}")
        else:
            print(f"⚠ DLL not found: {dll}")

    print("=== Done ===")

    # # copy scene.bin
    # shutil.copy("scene.bin", OUTPUT_DIR)
    # print(f"✔ Copied scene.bin → {src.name}")



if __name__ == "__main__":
    build()
