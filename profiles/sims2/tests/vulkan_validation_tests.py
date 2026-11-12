"""Require clean Vulkan validation, including the point-list pipeline."""
import os
import subprocess
import sys

env = dict(os.environ, SIMS2_GPU_DEBUG="1", VK_INSTANCE_LAYERS="VK_LAYER_KHRONOS_validation")
result = subprocess.run([sys.argv[1]], env=env, stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT, timeout=60)
text = result.stdout.decode("utf-8", errors="replace")
if result.returncode or "Validation layers enabled" not in text or "Validation Error" in text or "VUID-" in text:
    print(text)
    raise SystemExit("Vulkan validation failed or required layers were not enabled")
print("Renderer pipelines and rasterized pixels passed Vulkan validation")
