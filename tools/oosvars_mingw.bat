@set oos_path=D:\UNIX V6++test\oos
@set mingw_path=D:\UNIX V6++V1\MinGW\bin
@set nasm_path=D:\UNIX V6++V1\NASM
@set bochs_path=D:\UNIX V6++V1\Bochs-2.6
@set BXSHARE=%bochs_path%
@set partcopy_path=%oos_path%\tools\partcopy

@set path=%partcopy_path%;%bochs_path%;%nasm_path%;%mingw_path%;%oos_path%;%path%

@cls
@echo Setting develop and build environment for UnixV6++.