"""Exercise real Windows setup process ownership without game inputs."""
import ctypes
from ctypes import wintypes
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
from setup_process import external_dll_search
from build_from_iso import CompilationProgress, promote_generated
from runtime_package import stage_companion, RUNTIME_FILES


@unittest.skipUnless(os.name == "nt", "Windows process/DLL behavior")
class SetupProcessTests(unittest.TestCase):
    def test_regeneration_preserves_unchanged_objects(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); staging = root / "staging"; generated = root / "generated"
            staging.mkdir(); generated.mkdir()
            (generated / "generated_unit_0000.cpp").write_text("unchanged")
            (generated / "generated_unit_0001.cpp").write_text("old")
            (generated / "generated_unit_0002.cpp").write_text("obsolete")
            os.utime(generated / "generated_unit_0000.cpp", ns=(1000000000, 1000000000))
            (staging / "generated_unit_0000.cpp").write_text("unchanged")
            (staging / "generated_unit_0001.cpp").write_text("new")
            (staging / "generated_registry.cpp").write_text("registry")
            promote_generated(staging, generated)
            self.assertEqual((generated / "generated_unit_0000.cpp").stat().st_mtime_ns, 1000000000)
            self.assertEqual((generated / "generated_unit_0001.cpp").read_text(), "new")
            self.assertFalse((generated / "generated_unit_0002.cpp").exists())
            self.assertTrue((generated / "generated_registry.cpp").exists())

    def test_companion_dependencies_are_isolated(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);source=root/"source";install=root/"install"
            source.mkdir();install.mkdir()
            (install/"SDL3.dll").write_bytes(b"primary")
            for name in RUNTIME_FILES:(source/name).write_bytes(name.encode())
            target=stage_companion(source/"sims2_probe.exe",install,"vulkan")
            self.assertEqual((install/"SDL3.dll").read_bytes(),b"primary")
            self.assertEqual({p.name for p in target.iterdir()},set(RUNTIME_FILES))
            (source/"SDL3.dll").unlink()
            with self.assertRaises(RuntimeError):stage_companion(source/"sims2_probe.exe",install,"d3d11")
            self.assertFalse((install/"d3d11").exists())
    def test_compiler_progress_is_monotonic(self):
        updates = []
        progress = CompilationProgress(575, lambda value, text: updates.append((value, text)))
        for line in ("  generated_unit_0000.cpp\n", "  generated_unit_0032.cpp\n",
                     "  generated_unit_0016.cpp\n", "warning: unrelated output\n",
                     "  generated_unit_0574.cpp\n"):
            progress(line)
        values = [value for value, _ in updates]
        self.assertGreater(len(values), 1)
        self.assertEqual(values, sorted(values))
        self.assertTrue(all(0.25 < value < 0.90 for value in values))
        self.assertIn("575", updates[-1][1])
        updates.clear()
        for start in (0.25,0.57):
            part=CompilationProgress(575,lambda value,text:updates.append((value,text)),start=start,span=0.32)
            for unit in (0,574):part(f"generated_unit_{unit:04}.cpp")
        self.assertEqual([v for v,_ in updates],sorted(v for v,_ in updates))
        self.assertAlmostEqual(updates[-1][0],0.89)

    def test_normal_completion_preserves_success(self):
        code = f"""
import subprocess,sys
sys.path.insert(0,{str(TOOLS)!r})
from setup_process import own_process_job
with own_process_job():
    subprocess.run([sys.executable,'-c','print(\"child completed\")'],check=True)
print('setup completed')
"""
        result = subprocess.run([sys.executable, "-c", code], capture_output=True,
                                text=True, timeout=15, creationflags=subprocess.CREATE_NO_WINDOW)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("setup completed", result.stdout)

    def test_forced_exit_terminates_owned_child(self):
        code = f"""
import os,subprocess,sys
sys.path.insert(0,{str(TOOLS)!r})
from setup_process import own_process_job
with own_process_job():
    child=subprocess.Popen([sys.executable,'-c','import time;time.sleep(60)'],
        stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,
        creationflags=subprocess.CREATE_NO_WINDOW)
    print(child.pid,flush=True)
    sys.stdin.readline()
    os._exit(0)
"""
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        kernel.OpenProcess.restype = wintypes.HANDLE
        kernel.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
        kernel.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        parent = subprocess.Popen([sys.executable, "-c", code], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                  text=True, creationflags=subprocess.CREATE_NO_WINDOW)
        handle = None
        try:
            child_id = int(parent.stdout.readline())
            # Retain a handle before exiting the parent, avoiding PID-reuse races.
            handle = kernel.OpenProcess(0x1001, False, child_id)
            self.assertTrue(handle, ctypes.get_last_error())
            parent.stdin.write("exit\n")
            parent.stdin.flush()
            self.assertEqual(parent.wait(timeout=10), 0)
            deadline = time.monotonic() + 5
            status = wintypes.DWORD()
            while True:
                self.assertTrue(kernel.GetExitCodeProcess(handle, ctypes.byref(status)))
                if status.value != 259 or time.monotonic() >= deadline:
                    break
                time.sleep(0.02)
            self.assertNotEqual(status.value, 259, "Forced setup exit orphaned its child")
        finally:
            if parent.poll() is None:
                parent.kill()
                parent.wait(timeout=10)
            if handle:
                status = wintypes.DWORD()
                if kernel.GetExitCodeProcess(handle, ctypes.byref(status)) and status.value == 259:
                    kernel.TerminateProcess(handle, 1)
                kernel.CloseHandle(handle)
            for stream in (parent.stdin, parent.stdout, parent.stderr):
                stream.close()

    def test_frozen_external_dll_search_is_restored(self):
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.GetDllDirectoryW.argtypes = [wintypes.DWORD, wintypes.LPWSTR]
        kernel.GetDllDirectoryW.restype = wintypes.DWORD
        kernel.SetDllDirectoryW.argtypes = [wintypes.LPCWSTR]
        kernel.SetDllDirectoryW.restype = wintypes.BOOL

        def directory():
            buffer = ctypes.create_unicode_buffer(32768)
            count = kernel.GetDllDirectoryW(len(buffer), buffer)
            self.assertLess(count, len(buffer))
            return buffer.value

        original = directory()
        missing = object()
        old_frozen = getattr(sys, "frozen", missing)
        old_home = getattr(sys, "_MEIPASS", missing)
        temporary_root = Path(tempfile.gettempdir()).resolve()
        with tempfile.TemporaryDirectory(prefix="sims2-setup-dll-", dir=temporary_root) as name:
            self.assertTrue(Path(name).resolve().is_relative_to(temporary_root))
            try:
                self.assertTrue(kernel.SetDllDirectoryW(name))
                sys.frozen, sys._MEIPASS = True, name
                with external_dll_search():
                    self.assertEqual(directory(), "")
                self.assertEqual(directory(), name)
            finally:
                kernel.SetDllDirectoryW(original or None)
                for key, old in (("frozen", old_frozen), ("_MEIPASS", old_home)):
                    if old is missing:
                        delattr(sys, key)
                    else:
                        setattr(sys, key, old)


if __name__ == "__main__":
    unittest.main()
