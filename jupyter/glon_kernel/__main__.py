"""python -m glon_kernel -f {connection_file} -- launch the Glon kernel."""

from ipykernel.kernelapp import IPKernelApp

from .kernel import GlonKernel

if __name__ == "__main__":
    IPKernelApp.launch_instance(kernel_class=GlonKernel)
