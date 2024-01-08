INCLUDE = -I ./3rd_party/ffmpeg/include
LIBRARY = -L ./3rd_party/ffmpeg/lib
LINK = -l avformat -l avcodec -l avdevice -l avfilter -l avutil -l postproc -l swresample -l swscale

default:
	g++ main.cpp -o main -g ${INCLUDE} ${LIBRARY} ${LINK}
