"""glon_kernel -- a Jupyter kernel for persistent Glon sessions.

kernel.py   the ipykernel wrapper (GlonKernel); needs ipykernel
host.py     the stdlib-only client for the native session host
install.py  kernelspec installer (python -m glon_kernel.install)

Importing the package imports nothing from Jupyter, so the host-level tests
run without ipykernel installed.
"""
