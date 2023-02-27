/* -*- c++ -*- */
/* 
 * Copyright 2017 Ron Economos.
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

#ifndef INCLUDED_MPE_MPE_CONFIG_H
#define INCLUDED_MPE_MPE_CONFIG_H

namespace gr {
  namespace mpe {
    enum mpe_ping_reply_t {
      PING_REPLY_OFF = 0,
      PING_REPLY_ON,
    };

    enum mpe_ipaddr_spoof_t {
      IPADDR_SPOOF_OFF = 0,
      IPADDR_SPOOF_ON,
    };

  } // namespace mpe
} // namespace gr

typedef gr::mpe::mpe_ping_reply_t mpe_ping_reply_t;
typedef gr::mpe::mpe_ipaddr_spoof_t mpe_ipaddr_spoof_t;

#endif /* INCLUDED_MPE_MPE_CONFIG_H */

