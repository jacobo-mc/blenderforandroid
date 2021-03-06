LCGDIR = '#../lib/windows'
LIBDIR = '${LCGDIR}'

BF_PYTHON = LIBDIR + '/python'
BF_PYTHON_VERSION = '3.2'
BF_PYTHON_INC = '${BF_PYTHON}/include/python${BF_PYTHON_VERSION}'
BF_PYTHON_BINARY = 'python'
BF_PYTHON_LIB = 'python${BF_PYTHON_VERSION[0]}${BF_PYTHON_VERSION[2]}mw'
BF_PYTHON_DLL = 'python31'
BF_PYTHON_LIBPATH = '${BF_PYTHON}/lib'

WITH_BF_OPENAL = True
WITH_BF_STATICOPENAL = False
BF_OPENAL = LIBDIR + '/openal'
BF_OPENAL_INC = '${BF_OPENAL}/include'
BF_OPENAL_LIB = 'OpenAL32 wrap_oal'
BF_OPENAL_LIBPATH = '${BF_OPENAL}/lib'
# Warning, this static lib configuration is untested! users of this OS please confirm.
BF_OPENAL_LIB_STATIC = '${BF_OPENAL}/lib/libopenal.a'

# Warning, this static lib configuration is untested! users of this OS please confirm.
BF_CXX = '/usr'
WITH_BF_STATICCXX = False
BF_CXX_LIB_STATIC = '${BF_CXX}/lib/libstdc++.a'

WITH_BF_SDL = True
BF_SDL = LIBDIR + '/sdl'
BF_SDL_INC = '${BF_SDL}/include'
BF_SDL_LIB = 'SDL'
BF_SDL_LIBPATH = '${BF_SDL}/lib'

WITH_BF_JACK = False
BF_JACK = LIBDIR + '/jack'
BF_JACK_INC = '${BF_JACK}/include'
BF_JACK_LIB = 'jack'
BF_JACK_LIBPATH = '${BF_JACK}/lib'

WITH_BF_SNDFILE = False
BF_SNDFILE = LIBDIR + '/sndfile'
BF_SNDFILE_INC = '${BF_SNDFILE}/include'
BF_SNDFILE_LIB = 'sndfile-1'
BF_SNDFILE_LIBPATH = '${BF_SNDFILE}/lib'

BF_PTHREADS = LIBDIR + '/pthreads'
BF_PTHREADS_INC = '${BF_PTHREADS}/include'
BF_PTHREADS_LIB = 'pthreadGC2'
BF_PTHREADS_LIBPATH = '${BF_PTHREADS}/lib'

WITH_BF_OPENEXR = True
WITH_BF_STATICOPENEXR = False
BF_OPENEXR = LIBDIR + '/gcc/openexr'
BF_OPENEXR_INC = '${BF_OPENEXR}/include ${BF_OPENEXR}/include/OpenEXR'
BF_OPENEXR_LIB = ' Half IlmImf Iex IlmThread '
BF_OPENEXR_LIBPATH = '${BF_OPENEXR}/lib'
# Warning, this static lib configuration is untested! users of this OS please confirm.
BF_OPENEXR_LIB_STATIC = '${BF_OPENEXR}/lib/libHalf.a ${BF_OPENEXR}/lib/libIlmImf.a ${BF_OPENEXR}/lib/libIex.a ${BF_OPENEXR}/lib/libImath.a ${BF_OPENEXR}/lib/libIlmThread.a'

WITH_BF_DDS = True

WITH_BF_JPEG = True
BF_JPEG = LIBDIR + '/jpeg'
BF_JPEG_INC = '${BF_JPEG}/include'
BF_JPEG_LIB = 'libjpeg'
BF_JPEG_LIBPATH = '${BF_JPEG}/lib'

WITH_BF_PNG = True
BF_PNG = LIBDIR + '/png'
BF_PNG_INC = '${BF_PNG}/include'
BF_PNG_LIB = 'png'
BF_PNG_LIBPATH = '${BF_PNG}/lib'

WITH_BF_TIFF = True
BF_TIFF = LIBDIR + '/gcc/tiff'
BF_TIFF_INC = '${BF_TIFF}/include'
BF_TIFF_LIB = 'tiff'
BF_TIFF_LIBPATH = '${BF_TIFF}/lib'

WITH_BF_ZLIB = True
BF_ZLIB = LIBDIR + '/zlib'
BF_ZLIB_INC = '${BF_ZLIB}/include'
BF_ZLIB_LIB = 'libz'
BF_ZLIB_LIBPATH = '${BF_ZLIB}/lib'

WITH_BF_INTERNATIONAL = True

WITH_BF_GAMEENGINE = True
WITH_BF_PLAYER = False
WITH_BF_OCEANSIM = True

WITH_BF_BULLET = True
BF_BULLET = '#extern/bullet2/src'
BF_BULLET_INC = '${BF_BULLET}'
BF_BULLET_LIB = 'extern_bullet'

BF_WINTAB = LIBDIR + '/wintab'
BF_WINTAB_INC = '${BF_WINTAB}/INCLUDE'

BF_WITH_FREETYPE = True
BF_FREETYPE = LIBDIR + '/gcc/freetype'
BF_FREETYPE_INC = '${BF_FREETYPE}/include ${BF_FREETYPE}/include/freetype2'
BF_FREETYPE_LIB = 'freetype'
BF_FREETYPE_LIBPATH = '${BF_FREETYPE}/lib'

WITH_BF_QUICKTIME = False
BF_QUICKTIME = '/usr/local'
BF_QUICKTIME_INC = '${BF_QUICKTIME}/include'

WITH_BF_ICONV = False
BF_ICONV = LIBDIR + "/gcc/iconv"
BF_ICONV_INC = '${BF_ICONV}/include'
BF_ICONV_LIB = 'iconv'
BF_ICONV_LIBPATH = '${BF_ICONV}/lib'

WITH_BF_BINRELOC = False

# enable ffmpeg  support
WITH_BF_FFMPEG = True
BF_FFMPEG = LIBDIR + '/ffmpeg'
BF_FFMPEG_LIB = 'avformat-53 avcodec-53 avdevice-53 avutil-51 swscale-2'
BF_FFMPEG_DLL = '${BF_FFMPEG_LIBPATH}/avformat-53.dll ${BF_FFMPEG_LIBPATH}/avcodec-53.dll ${BF_FFMPEG_LIBPATH}/avdevice-53.dll ${BF_FFMPEG_LIBPATH}/avutil-51.dll ${BF_FFMPEG_LIBPATH}/swscale-2.dll'
BF_FFMPEG_INC = '${BF_FFMPEG}/include'
BF_FFMPEG_LIBPATH = '${BF_FFMPEG}/lib'
BF_FFMPEG_DLL = '${BF_FFMPEG_LIBPATH}/avformat-53.dll ${BF_FFMPEG_LIBPATH}/avcodec-53.dll ${BF_FFMPEG_LIBPATH}/avdevice-53.dll ${BF_FFMPEG_LIBPATH}/avutil-51.dll ${BF_FFMPEG_LIBPATH}/swscale-2.dll'

WITH_BF_OPENJPEG = True
BF_OPENJPEG = '#extern/libopenjpeg'
BF_OPENJPEG_LIB = ''
BF_OPENJPEG_INC = '${BF_OPENJPEG}'
BF_OPENJPEG_LIBPATH='${BF_OPENJPEG}/lib'

WITH_BF_FFTW3 = False
BF_FFTW3 = LIBDIR + '/gcc/fftw3'
BF_FFTW3_INC = '${BF_FFTW3}/include'
BF_FFTW3_LIB = 'fftw3'
BF_FFTW3_LIBPATH = '${BF_FFTW3}/lib'

WITH_BF_REDCODE = False
BF_REDCODE_INC = '#extern'

# Mesa Libs should go here if your using them as well....
WITH_BF_STATICOPENGL = False
BF_OPENGL = 'C:\\MingW'
BF_OPENGL_INC = '${BF_OPENGL}/include'
BF_OPENGL_LIBINC = '${BF_OPENGL}/lib'
BF_OPENGL_LIB = 'opengl32 glu32'
BF_OPENGL_LIB_STATIC = [ '${BF_OPENGL}/lib/libGL.a', '${BF_OPENGL}/lib/libGLU.a']

WITH_BF_OPENMP = False
BF_OPENMP = LIBDIR + '/gcc/gomp'
BF_OPENMP_INC = '${BF_OPENMP}/include'
BF_OPENMP_LIBPATH = '${BF_OPENMP}/lib'

WITH_BF_COLLADA = False
BF_COLLADA = '#source/blender/collada'
BF_COLLADA_INC = '${BF_COLLADA}'
BF_COLLADA_LIB = 'bf_collada'

BF_OPENCOLLADA = LIBDIR + '/gcc/opencollada'
BF_OPENCOLLADA_INC = '${BF_OPENCOLLADA}/include'
BF_OPENCOLLADA_LIB = 'OpenCOLLADAStreamWriter OpenCOLLADASaxFrameworkLoader OpenCOLLADAFramework OpenCOLLADABaseUtils GeneratedSaxParser UTF MathMLSolver expat pcre buffer ftoa'
BF_OPENCOLLADA_LIBPATH = '${BF_OPENCOLLADA}/lib ${BF_ICONV_LIBPATH}'

#Ray trace optimization
WITH_BF_RAYOPTIMIZATION = True
BF_RAYOPTIMIZATION_SSE_FLAGS = ['-msse']

CCFLAGS = [ '-pipe', '-funsigned-char', '-fno-strict-aliasing' ]
CXXFLAGS = []

CPPFLAGS = ['-DWIN32', '-DFREE_WINDOWS', '-D_LARGEFILE_SOURCE', '-D_FILE_OFFSET_BITS=64', '-D_LARGEFILE64_SOURCE']
REL_CFLAGS = []
REL_CXXFLAGS = []
REL_CCFLAGS = ['-DNDEBUG',  '-O2']
C_WARN = ['-Wall', '-Wstrict-prototypes', '-Wno-char-subscripts', '-Wdeclaration-after-statement']

CC_WARN = [ '-Wall' ]

LLIBS = [ '-ldxguid', '-lgdi32', '-lmsvcrt', '-lwinmm', '-lmingw32', '-lm', '-lws2_32', '-lz', '-lstdc++', '-luuid', '-lole32'] #'-lutil', '-lc', '-lm', '-ldl', '-lpthread' ]

PLATFORM_LINKFLAGS = ['--stack,2097152']

BF_DEBUG = False
BF_DEBUG_CCFLAGS = ['-g', '-D_DEBUG']

BF_PROFILE = False
BF_PROFILE_CCFLAGS = ['-pg','-g']
BF_PROFILE_LINKFLAGS = ['-pg']

BF_BUILDDIR = '../build/linuxcross'
BF_INSTALLDIR='../install/linuxcross'
