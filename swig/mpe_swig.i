/* -*- c++ -*- */

#define MPE_API

%include "gnuradio.i"			// the common stuff

//load generated python docstrings
%include "mpe_swig_doc.i"

%{
#include "mpe/mpe_source.h"
%}


%include "mpe/mpe_source.h"
GR_SWIG_BLOCK_MAGIC2(mpe, mpe_source);
