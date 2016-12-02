/* -*- c++ -*- */
/* 
 * Copyright 2016 Ron Economos.
 * 
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this software; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */


#ifndef INCLUDED_MPE_MPE_SOURCE_H
#define INCLUDED_MPE_MPE_SOURCE_H

#include <mpe/api.h>
#include <gnuradio/sync_block.h>

namespace gr {
  namespace mpe {

    /*!
     * \brief <+description of block+>
     * \ingroup mpe
     *
     */
    class MPE_API mpe_source : virtual public gr::sync_block
    {
     public:
      typedef boost::shared_ptr<mpe_source> sptr;

      /*!
       * \brief Return a shared_ptr to a new instance of mpe::mpe_source.
       *
       * To avoid accidental use of raw pointers, mpe::mpe_source's
       * constructor is in a private implementation
       * class. mpe::mpe_source::make is the public interface for
       * creating new instances.
       */
      static sptr make(char *mac_address);
    };

  } // namespace mpe
} // namespace gr

#endif /* INCLUDED_MPE_MPE_SOURCE_H */

