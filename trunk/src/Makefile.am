lib_LTLIBRARIES=libv804_stream.la
libv804_stream_la_SOURCES=v804_stream.c

testdir=$(prefix)/etc/tea/test
test_DATA=\
	  v804_stream.xml.m

#bin_PROGRAMS=\
#	     test_stream \
#	     test_loop

#test_stream_SOURCES = test_stream.c
#test_loop_SOURCES = test_loop.c

bin_PROGRAMS=\
	     test_read
test_read_SOURCES = test_read.c

commondir=$(prefix)/etc/tea/common
common_DATA=\
	  v804_stream.xml


schemadir=$(prefix)/etc/tea/schema
schema_DATA=\
	    v804_stream.s
