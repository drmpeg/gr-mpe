/* -*- c++ -*- */
/* 
 * Copyright 2016,2017 Ron Economos.
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gnuradio/io_signature.h>
#include "mpe_source_impl.h"

#define DEFAULT_IF "tap0"
#define FILTER "ether src "
#define MPE_PID 0x35
#undef DEBUG

namespace gr {
  namespace mpe {

    mpe_source::sptr
    mpe_source::make(char *mac_address, mpe_ping_reply_t ping_reply, mpe_ipaddr_spoof_t ipaddr_spoof, char *src_address, char *dst_address)
    {
      return gnuradio::get_initial_sptr
        (new mpe_source_impl(mac_address, ping_reply, ipaddr_spoof, src_address, dst_address));
    }

    /*
     * The private constructor
     */
    mpe_source_impl::mpe_source_impl(char *mac_address, mpe_ping_reply_t ping_reply, mpe_ipaddr_spoof_t ipaddr_spoof, char *src_address, char *dst_address)
      : gr::sync_block("mpe_source",
              gr::io_signature::make(0, 0, 0),
              gr::io_signature::make(1, 1, sizeof(unsigned char)))
    {
      TS_HEADER tsHeader;
      PAT_HEADER patHeader;
      PAT_ELEMENT patElement;
      PMT_HEADER pmtHeader;
      PMT_ELEMENT pmtElement;
      PMT_STREAM_DESCRIPTOR streamDesc;
      unsigned char tempBuffer[MPEG2_PACKET_SIZE];
      int offset, temp_offset;
      int pidPAT = 0;
      int pidPMT = 0x30;
      int pidVID = 0x31;
      int pidAUD = 0x34;
      int pidMPE = MPE_PID;
      int pidNULL = 0x1fff;
      int programNum = 1;
      int crc32;
      char errbuf[PCAP_ERRBUF_SIZE];
      char dev[IFNAMSIZ];
      struct bpf_program fp;
      bpf_u_int32 netp = 0;
      char filter[50];
      struct ifreq ifr;
      int fd, err;

      pat_count = 0;
      pmt_count = 0;
      packet_count = 0;
      mpe_continuity_counter = 0;
      next_packet_valid = FALSE;
      ping_reply_mode = ping_reply;
      ipaddr_spoof_mode = ipaddr_spoof;
      inet_pton(AF_INET, src_address, &src_addr);
      inet_pton(AF_INET, dst_address, &dst_addr);
      crc32_init();

      /* null packet */
      offset = 0;
      tsHeader.sync_byte = 0x47;
      tsHeader.transport_error_indicator = 0x0;
      tsHeader.payload_unit_start_indicator = 0x0;
      tsHeader.transport_priority = 0x0;
      tsHeader.pid_12to8 = ((pidNULL) >> 8) & 0x1f;
      tsHeader.pid_7to0 = (pidNULL) & 0xff;
      tsHeader.transport_scrambling_control = 0x0;
      tsHeader.adaptation_field_control = 0x1;
      tsHeader.continuity_counter = 0;
      memcpy(&stuffing[offset], (unsigned char *) &tsHeader, TS_HEADER_SIZE);
      offset += TS_HEADER_SIZE;

      memset(&stuffing[offset], 0xff, MPEG2_PACKET_SIZE - offset);

      /* PAT packet */
      offset = 0;
      tsHeader.sync_byte = 0x47;
      tsHeader.transport_error_indicator = 0x0;
      tsHeader.payload_unit_start_indicator = 0x1;
      tsHeader.transport_priority = 0x1;
      tsHeader.pid_12to8 = ((pidPAT) >> 8) & 0x1f;
      tsHeader.pid_7to0 = (pidPAT) & 0xff;
      tsHeader.transport_scrambling_control = 0x0;
      tsHeader.adaptation_field_control = 0x1;
      tsHeader.continuity_counter = 0;
      memcpy(&pat[offset], (unsigned char *)&tsHeader, TS_HEADER_SIZE);
      offset += TS_HEADER_SIZE;

      pat[offset] = 0x0;
      offset += 1;

      temp_offset = 8;
      patElement.program_number_h = (programNum >> 8) & 0xff;
      patElement.program_number_l = programNum & 0xff;
      patElement.reserved2 = 0x7;
      patElement.program_map_PID_h = (pidPMT >> 8) & 0x1f;
      patElement.program_map_PID_l = pidPMT & 0xff;
      memcpy(&tempBuffer[temp_offset], (unsigned char *) &patElement, PAT_ELEMENT_SIZE);
      temp_offset += PAT_ELEMENT_SIZE;

      patHeader.table_id = 0x00;
      patHeader.section_syntax_indicator = 0x1;
      patHeader.b0 = 0x0;
      patHeader.reserved0 = 0x3;
      patHeader.section_length_h = ((temp_offset - 3 + sizeof(crc32)) >> 8) & 0xf;
      patHeader.section_length_l = (temp_offset - 3 + sizeof(crc32)) & 0xff;

      patHeader.transport_stream_id_h = 0x00;
      patHeader.transport_stream_id_l = 0x00;
      patHeader.reserved1 = 0x3;
      patHeader.version_number = 0;
      patHeader.current_next_indicator = 1;
      patHeader.section_number = 0x0;
      patHeader.last_section_number = 0x0;
      memcpy(&tempBuffer[0], (char *) &patHeader, PAT_HEADER_SIZE);

      memcpy(&pat[offset], &tempBuffer, temp_offset);
      offset += temp_offset;

      crc32 = crc32_calc(&tempBuffer[0], temp_offset);
      memcpy(&pat[offset], (unsigned char *) &crc32, sizeof(crc32));
      offset += sizeof(crc32);

      memset(&pat[offset], 0xff, MPEG2_PACKET_SIZE - offset);

      /* PMT packet */
      offset = 0;
      tsHeader.sync_byte = 0x47;
      tsHeader.transport_error_indicator = 0x0;
      tsHeader.payload_unit_start_indicator = 0x1;
      tsHeader.transport_priority = 0x1;
      tsHeader.pid_12to8 = ((pidPMT) >> 8) & 0x1f;
      tsHeader.pid_7to0 = (pidPMT) & 0xff;
      tsHeader.transport_scrambling_control = 0x0;
      tsHeader.adaptation_field_control = 0x1;
      tsHeader.continuity_counter = 0;
      memcpy(&pmt[offset], (unsigned char *)&tsHeader, TS_HEADER_SIZE);
      offset += TS_HEADER_SIZE;

      pmt[offset] = 0x0;
      offset += 1;

      /* audio stream */
      temp_offset = PMT_HEADER_SIZE;
      pmtElement.stream_type = 0x81;
      pmtElement.reserved0 = 0x7;
      pmtElement.elementary_PID_h = (pidAUD >> 8) & 0x1f;
      pmtElement.elementary_PID_l = pidAUD & 0xff;
      pmtElement.reserved1 = 0xf;
      pmtElement.ES_info_length_h = 0x00;
      pmtElement.ES_info_length_l = PMT_STREAM_DESCRIPTOR_SIZE;

      memcpy(&tempBuffer[temp_offset], (unsigned char *) &pmtElement, PMT_ELEMENT_SIZE);
      temp_offset += PMT_ELEMENT_SIZE;

      streamDesc.descriptor_tag = 0x52;
      streamDesc.descriptor_length = 0x01;
      streamDesc.component_tag = 0x10;
      memcpy(&tempBuffer[temp_offset], (unsigned char *)&streamDesc, PMT_STREAM_DESCRIPTOR_SIZE);
      temp_offset += PMT_STREAM_DESCRIPTOR_SIZE;

      /* video stream */
      pmtElement.stream_type = 0x2;
      pmtElement.reserved0 = 0x7;
      pmtElement.elementary_PID_h = (pidVID >> 8) & 0x1f;
      pmtElement.elementary_PID_l = pidVID & 0xff;
      pmtElement.reserved1 = 0xf;
      pmtElement.ES_info_length_h = 0x00;
      pmtElement.ES_info_length_l = PMT_STREAM_DESCRIPTOR_SIZE;

      memcpy(&tempBuffer[temp_offset], (unsigned char *) &pmtElement, PMT_ELEMENT_SIZE);
      temp_offset += PMT_ELEMENT_SIZE;

      streamDesc.descriptor_tag = 0x52;
      streamDesc.descriptor_length = 0x01;
      streamDesc.component_tag = 0x0;
      memcpy(&tempBuffer[temp_offset], (unsigned char *)&streamDesc, PMT_STREAM_DESCRIPTOR_SIZE);
      temp_offset += PMT_STREAM_DESCRIPTOR_SIZE;

      /* MPE stream */
      pmtElement.stream_type = 0x0d;
      pmtElement.reserved0 = 0x7;
      pmtElement.elementary_PID_h = (pidMPE >> 8) & 0x1f;
      pmtElement.elementary_PID_l = pidMPE & 0xff;
      pmtElement.reserved1 = 0xf;
      pmtElement.ES_info_length_h = 0x00;
      pmtElement.ES_info_length_l = PMT_STREAM_DESCRIPTOR_SIZE;

      memcpy(&tempBuffer[temp_offset], (unsigned char *) &pmtElement, PMT_ELEMENT_SIZE);
      temp_offset += PMT_ELEMENT_SIZE;

      streamDesc.descriptor_tag = 0x52;
      streamDesc.descriptor_length = 0x01;
      streamDesc.component_tag = 0x40;
      memcpy(&tempBuffer[temp_offset], (unsigned char *)&streamDesc, PMT_STREAM_DESCRIPTOR_SIZE);
      temp_offset += PMT_STREAM_DESCRIPTOR_SIZE;

      pmtHeader.table_id = 0x02;
      pmtHeader.section_syntax_indicator = 1;
      pmtHeader.b0  = 0;
      pmtHeader.reserved0 = 0x3;
      pmtHeader.section_length_h = ((temp_offset - 3 + sizeof(crc32)) >> 8) & 0xf;
      pmtHeader.section_length_l = (temp_offset - 3 + sizeof(crc32)) & 0xff;
      pmtHeader.program_number_h = (programNum >> 8) & 0xff;
      pmtHeader.program_number_l = programNum & 0xff;
      pmtHeader.reserved1 = 0x3;
      pmtHeader.version_number = 0;
      pmtHeader.current_next_indicator = 1;
      pmtHeader.section_number = 0x0;
      pmtHeader.last_section_number = 0x0;
      pmtHeader.reserved2 = 0x7;
      pmtHeader.PCR_PID_h = (pidVID >> 8) & 0x1f;
      pmtHeader.PCR_PID_l = pidVID & 0xff;
      pmtHeader.reserved3 = 0xF;
      pmtHeader.program_info_length_h = 0;
      pmtHeader.program_info_length_l = 0;
      memcpy(&tempBuffer[0], (char *) &pmtHeader, PMT_HEADER_SIZE);

      memcpy(&pmt[offset], tempBuffer, temp_offset);
      offset += temp_offset;

      crc32 = crc32_calc(tempBuffer, temp_offset);
      memcpy(&pmt[offset], (char *)&crc32, sizeof(crc32));
      offset += sizeof(crc32);

      memset(&pmt[offset], 0xff, MPEG2_PACKET_SIZE - offset);

      if ((fd = open("/dev/net/tun", O_RDWR)) == -1) {
        throw std::runtime_error("Error calling open()\n");
      }
      memset(&ifr, 0, sizeof(ifr));
      ifr.ifr_flags = IFF_TAP;
      strncpy(ifr.ifr_name, DEFAULT_IF, IFNAMSIZ);

      if ((err = ioctl(fd, TUNSETIFF, (void *) &ifr)) == -1) {
        close(fd);
        throw std::runtime_error("Error calling ioctl()\n");
      }

      strcpy(dev, DEFAULT_IF);
      descr = pcap_create(dev, errbuf);
      if (descr == NULL) {
        std::stringstream s;
        s << "Error calling pcap_create(): " << errbuf << std::endl;
        throw std::runtime_error(s.str());
      }
      if (pcap_set_promisc(descr, 0) != 0) {
        pcap_close(descr);
        throw std::runtime_error("Error calling pcap_set_promisc()\n");
      }
      if (pcap_set_timeout(descr, -1) != 0) {
        pcap_close(descr);
        throw std::runtime_error("Error calling pcap_set_timeout()\n");
      }
      if (pcap_set_snaplen(descr, 65536) != 0) {
        pcap_close(descr);
        throw std::runtime_error("Error calling pcap_set_snaplen()\n");
      }
      if (pcap_set_buffer_size(descr, 1024 * 1024 * 16) != 0) {
        pcap_close(descr);
        throw std::runtime_error("Error calling pcap_set_buffer_size()\n");
      }
      if (pcap_activate(descr) != 0) {
        pcap_close(descr);
        throw std::runtime_error("Error calling pcap_activate()\n");
      }
      strcpy(filter, FILTER);
      strcat(filter, mac_address);
      if (pcap_compile(descr, &fp, filter, 0, netp) == -1) {
        throw std::runtime_error("Error calling pcap_compile()\n");
      }
      if (pcap_setfilter(descr, &fp) == -1) {
        throw std::runtime_error("Error calling pcap_setfilter()\n");
      }

      set_output_multiple(MPEG2_PACKET_SIZE * 200);
    }

    /*
     * Our virtual destructor.
     */
    mpe_source_impl::~mpe_source_impl()
    {
      if (descr) {
        pcap_close(descr);
      }
    }

    int
    mpe_source_impl::crc32_calc(unsigned char *buf, int size)
    {
      int crc = 0xffffffffL;
      int reverse;

      for (int i = 0; i < size; i++) {
        crc = (crc << 8) ^ crc32_table[((crc >> 24) ^ buf[i]) & 0xff];
      }
      reverse = (crc & 0xff) << 24;
      reverse |= (crc & 0xff00) << 8;
      reverse |= (crc & 0xff0000) >> 8;
      reverse |= (crc & 0xff000000) >> 24;
      return (reverse);
    }

    int
    mpe_source_impl::crc32_calc_partial(unsigned char *buf, int size, int crc)
    {
      for (int i = 0; i < size; i++) {
        crc = (crc << 8) ^ crc32_table[((crc >> 24) ^ buf[i]) & 0xff];
      }
      return (crc);
    }

    int
    mpe_source_impl::crc32_calc_final(unsigned char *buf, int size, int crc)
    {
      int reverse;

      for (int i = 0; i < size; i++) {
        crc = (crc << 8) ^ crc32_table[((crc >> 24) ^ buf[i]) & 0xff];
      }
      reverse = (crc & 0xff) << 24;
      reverse |= (crc & 0xff00) << 8;
      reverse |= (crc & 0xff0000) >> 8;
      reverse |= (crc & 0xff000000) >> 24;
      return (reverse);
    }

    void
    mpe_source_impl::crc32_init(void)
    {
      unsigned int i, j, k;

      for (i = 0; i < 256; i++) {
        k = 0;
        for (j = (i << 24) | 0x800000; j != 0x80000000; j <<= 1) {
          k = (k << 1) ^ (((k ^ j) & 0x80000000) ? 0x04c11db7 : 0);
        }
        crc32_table[i] = k;
      }
    }

    int
    mpe_source_impl::checksum(unsigned short *addr, int count, int sum)
    {
      while (count > 1) {
        sum += *addr++;
        count -= 2;
      }
      if (count > 0) {
        sum += *(unsigned char *)addr;
      }
      sum = (sum & 0xffff) + (sum >> 16);
      sum += (sum >> 16);
      return (~sum);
    }

    inline void
    mpe_source_impl::ping_reply(void)
    {
      unsigned short *csum_ptr;
      unsigned short header_length, total_length, type_code, fragment_offset;
      int csum;
      struct ip *ip_ptr;
      unsigned char *saddr_ptr, *daddr_ptr;
      unsigned char addr[sizeof(in_addr)];

      /* jam ping reply and calculate new checksum */
      ip_ptr = (struct ip*)(packet_save + sizeof(struct ether_header));
      csum_ptr = (unsigned short *)ip_ptr;
      header_length = (*csum_ptr & 0xf) * 4;
      csum_ptr = &ip_ptr->ip_len;
      total_length = ((*csum_ptr & 0xff) << 8) | ((*csum_ptr & 0xff00) >> 8);
      csum_ptr = &ip_ptr->ip_off;
      fragment_offset = ((*csum_ptr & 0xff) << 8) | ((*csum_ptr & 0xff00) >> 8);

      csum_ptr = (unsigned short *)(packet_save + sizeof(struct ether_header) + sizeof(struct ip));
      type_code = *csum_ptr;
      type_code = (type_code & 0xff00) | 0x0;
      if ((fragment_offset & 0x1fff) == 0) {
        *csum_ptr++ = type_code;
        *csum_ptr = 0x0000;
        csum_ptr = (unsigned short *)(packet_save + sizeof(struct ether_header) + sizeof(struct ip));
        csum = checksum(csum_ptr, total_length - header_length, 0);
        csum_ptr++;
        *csum_ptr = csum;
      }

      /* swap IP adresses */
      saddr_ptr = (unsigned char *)&ip_ptr->ip_src;
      daddr_ptr = (unsigned char *)&ip_ptr->ip_dst;
      for (unsigned int i = 0; i < sizeof(in_addr); i++) {
        addr[i] = *daddr_ptr++;
      }
      daddr_ptr = (unsigned char *)&ip_ptr->ip_dst;
      for (unsigned int i = 0; i < sizeof(in_addr); i++) {
        *daddr_ptr++ = *saddr_ptr++;
      }
      saddr_ptr = (unsigned char *)&ip_ptr->ip_src;
      for (unsigned int i = 0; i < sizeof(in_addr); i++) {
        *saddr_ptr++ = addr[i];
      }
    }

    inline void
    mpe_source_impl::ipaddr_spoof(void)
    {
      unsigned short *csum_ptr;
      unsigned short header_length, fragment_offset;
      int csum;
      struct ip *ip_ptr;
      unsigned char *saddr_ptr, *daddr_ptr;

      ip_ptr = (struct ip*)(packet_save + sizeof(struct ether_header));

      saddr_ptr = (unsigned char *)&ip_ptr->ip_src;
      for (unsigned int i = 0; i < sizeof(in_addr); i++) {
        *saddr_ptr++ = src_addr[i];
      }

      daddr_ptr = (unsigned char *)&ip_ptr->ip_dst;
      for (unsigned int i = 0; i < sizeof(in_addr); i++) {
        *daddr_ptr++ = dst_addr[i];
      }

      csum_ptr = (unsigned short *)ip_ptr;
      header_length = (*csum_ptr & 0xf) * 4;
      csum_ptr = &ip_ptr->ip_off;
      fragment_offset = ((*csum_ptr & 0xff) << 8) | ((*csum_ptr & 0xff00) >> 8);

      if ((fragment_offset & 0x1fff) == 0) {
        csum_ptr = &ip_ptr->ip_sum;
        *csum_ptr = 0x0000;
        csum_ptr = (unsigned short *)ip_ptr;
        csum = checksum(csum_ptr, header_length, 0);
        csum_ptr = &ip_ptr->ip_sum;
        *csum_ptr = csum;

        csum_ptr = (unsigned short *)(packet_save + sizeof(struct ether_header) + sizeof(struct ip) + 6);
        *csum_ptr = 0x0000;
      }
    }

    inline void
    mpe_source_impl::dump_packet(void)
    {
#ifdef DEBUG
      printf("\n");
      for (int i = 0; i < MPEG2_PACKET_SIZE; i++) {
        if (i % 16 == 0) {
          printf("\n");
        }
        printf("0x%02x:", mpe[i]);
      }
      printf("\n");
#endif
    }
    int
    mpe_source_impl::work(int noutput_items,
        gr_vector_const_void_star &input_items,
        gr_vector_void_star &output_items)
    {
      unsigned char *out = (unsigned char *) output_items[0];
      int size = noutput_items;
      int produced = 0;
      unsigned char temp, continuity_counter;
      struct pcap_pkthdr hdr;
      struct ether_header *eptr;
      unsigned char *ptr;
      int crc32;
      TS_HEADER tsHeader;
      DATAGRAM_HEADER datagram;
      int pidMPE = MPE_PID;
      unsigned int remainder, offset, temp_offset, length;

      while (produced + MPEG2_PACKET_SIZE <= size) {
        pat_count++;
        pmt_count++;
        if (pat_count >= 500) {
          pat_count = 0;
          memcpy(&out[produced], &pat[0], MPEG2_PACKET_SIZE);
          temp = pat[3];
          continuity_counter = temp & 0xf;
          continuity_counter = (continuity_counter + 1) & 0xf;
          temp = (temp & 0xf0) | continuity_counter;
          pat[3] = temp;
          produced += MPEG2_PACKET_SIZE;
          if (produced == size) {
            break;
          }
        }
        else if (pmt_count >= 500) {
          pmt_count = 0;
          memcpy(&out[produced], &pmt[0], MPEG2_PACKET_SIZE);
          temp = pmt[3];
          continuity_counter = temp & 0xf;
          continuity_counter = (continuity_counter + 1) & 0xf;
          temp = (temp & 0xf0) | continuity_counter;
          pmt[3] = temp;
          produced += MPEG2_PACKET_SIZE;
          if (produced == size) {
            break;
          }
        }
        if (packet_count == 0) {
          if (next_packet_valid == FALSE) {
            packet = pcap_next(descr, &hdr);
          }
          if (packet != NULL) {
            next_packet_valid = FALSE;
            memcpy(packet_save, packet, hdr.len);
            if (hdr.len - sizeof(struct ether_header) + sizeof(crc32) <= MPE_PAYLOAD_PP_SIZE) {
              /* one TS packet */
              offset = 0;
              tsHeader.sync_byte = 0x47;
              tsHeader.transport_error_indicator = 0x0;
              tsHeader.payload_unit_start_indicator = 0x1;
              tsHeader.transport_priority = 0x1;
              tsHeader.pid_12to8 = ((pidMPE) >> 8) & 0x1f;
              tsHeader.pid_7to0 = (pidMPE) & 0xff;
              tsHeader.transport_scrambling_control = 0x0;
              tsHeader.adaptation_field_control = 0x1;
              tsHeader.continuity_counter = mpe_continuity_counter & 0xf;
              mpe_continuity_counter = (mpe_continuity_counter + 1) & 0xf;
              memcpy(&mpe[offset], (unsigned char *)&tsHeader, TS_HEADER_SIZE);
              offset += TS_HEADER_SIZE;

              mpe[offset++] = 0x0;    /* Payload Pointer */

              length = (hdr.len - sizeof(struct ether_header)) + (DATAGRAM_HEADER_SIZE - 3) + sizeof(crc32);

              datagram.table_id = 0x3e;   /* DSM-CC section */
              datagram.section_syntax_indicator = 1;
              datagram.private_indicator = 0;
              datagram.reserved0 = 0x3;
              datagram.section_length_h = (length >> 8) & 0xf;
              datagram.section_length_l = length & 0xff;
              datagram.reserved1 = 0x3;
              datagram.payload_scrambling_control = 0;
              datagram.address_scrambling_control = 0;
              datagram.LLC_SNAP_FLAG = 0;
              datagram.current_next_indicator = 1;
              datagram.section_number = 0;
              datagram.last_section_number = 0;
              eptr = (struct ether_header *)packet_save;
              ptr = eptr->ether_dhost;

              datagram.MAC_address_1 = *ptr++;
              datagram.MAC_address_2 = *ptr++;
              datagram.MAC_address_3 = *ptr++;
              datagram.MAC_address_4 = *ptr++;
              datagram.MAC_address_5 = *ptr++;
              datagram.MAC_address_6 = *ptr++;

              memcpy(&mpe[offset], (unsigned char *)&datagram, DATAGRAM_HEADER_SIZE);
              offset += DATAGRAM_HEADER_SIZE;

              if (ping_reply_mode) {
                ping_reply();
              }
              if (ipaddr_spoof_mode) {
                ipaddr_spoof();
              }

              ptr = (unsigned char *)(packet_save + sizeof(struct ether_header));
              for (unsigned int i = 0; i < hdr.len - sizeof(struct ether_header); i++) {
                mpe[offset++] = *ptr++;
              }
              crc32 = crc32_calc(&mpe[MPE_PAYLOAD_PP_OFFSET], offset - MPE_PAYLOAD_PP_OFFSET);
              memcpy(&mpe[offset], (unsigned char *) &crc32, sizeof(crc32));
              offset += sizeof(crc32);

              memset(&mpe[offset], 0xff, MPEG2_PACKET_SIZE - offset);
              dump_packet();

              memcpy(&out[produced], &mpe[0], MPEG2_PACKET_SIZE);
              produced += MPEG2_PACKET_SIZE;
              if (produced == size) {
                break;
              }
            }
            else {
              /* multiple TS packets */
              offset = 0;
              tsHeader.sync_byte = 0x47;
              tsHeader.transport_error_indicator = 0x0;
              tsHeader.payload_unit_start_indicator = 0x1;
              tsHeader.transport_priority = 0x1;
              tsHeader.pid_12to8 = ((pidMPE) >> 8) & 0x1f;
              tsHeader.pid_7to0 = (pidMPE) & 0xff;
              tsHeader.transport_scrambling_control = 0x0;
              tsHeader.adaptation_field_control = 0x1;
              tsHeader.continuity_counter = mpe_continuity_counter & 0xf;
              mpe_continuity_counter = (mpe_continuity_counter + 1) & 0xf;
              memcpy(&mpe[offset], (unsigned char *)&tsHeader, TS_HEADER_SIZE);
              offset += TS_HEADER_SIZE;

              mpe[offset++] = 0x0;    /* Payload Pointer */

              length = (hdr.len - sizeof(struct ether_header)) + (DATAGRAM_HEADER_SIZE - 3) + sizeof(crc32);

              datagram.table_id = 0x3e;   /* DSM-CC section */
              datagram.section_syntax_indicator = 1;
              datagram.private_indicator = 0;
              datagram.reserved0 = 0x3;
              datagram.section_length_h = (length >> 8) & 0xf;
              datagram.section_length_l = length & 0xff;
              datagram.reserved1 = 0x3;
              datagram.payload_scrambling_control = 0;
              datagram.address_scrambling_control = 0;
              datagram.LLC_SNAP_FLAG = 0;
              datagram.current_next_indicator = 1;
              datagram.section_number = 0;
              datagram.last_section_number = 0;
              eptr = (struct ether_header *)packet_save;
              ptr = eptr->ether_dhost;

              datagram.MAC_address_1 = *ptr++;
              datagram.MAC_address_2 = *ptr++;
              datagram.MAC_address_3 = *ptr++;
              datagram.MAC_address_4 = *ptr++;
              datagram.MAC_address_5 = *ptr++;
              datagram.MAC_address_6 = *ptr++;

              memcpy(&mpe[offset], (unsigned char *)&datagram, DATAGRAM_HEADER_SIZE);
              offset += DATAGRAM_HEADER_SIZE;

              if (ping_reply_mode) {
                ping_reply();
              }
              if (ipaddr_spoof_mode) {
                ipaddr_spoof();
              }

              ptr = (unsigned char *)(packet_save + sizeof(struct ether_header));
              if ((hdr.len - sizeof(struct ether_header)) < (MPE_PAYLOAD_PP_SIZE)) {
                for (unsigned int i = 0; i < hdr.len - sizeof(struct ether_header); i++) {
                  mpe[offset++] = *ptr++;
                }
              }
              else {
                for (int i = 0; i < MPE_PAYLOAD_PP_SIZE; i++) {
                  mpe[offset++] = *ptr++;
                }
              }
              crc32_partial = crc32_calc_partial(&mpe[MPE_PAYLOAD_PP_OFFSET], offset - MPE_PAYLOAD_PP_OFFSET, 0xffffffff);
              packet_ptr = ptr;
              packet_length = hdr.len - sizeof(struct ether_header) - MPE_PAYLOAD_PP_SIZE;
              shift = 3;
              if (packet_length < 0) {
                while (packet_length < 0) {
                  mpe[offset++] = (crc32_partial >> (shift * 8)) & 0xff;
                  packet_length++;
                  shift--;
                }
              }
              dump_packet();
              memcpy(&out[produced], &mpe[0], MPEG2_PACKET_SIZE);
              produced += MPEG2_PACKET_SIZE;
              if (hdr.len - sizeof(struct ether_header) + sizeof(crc32) > (MPE_PAYLOAD_PP_SIZE + MPE_PAYLOAD_SIZE)) {
                packet_count = ((hdr.len - sizeof(struct ether_header) + sizeof(crc32) - (MPE_PAYLOAD_PP_SIZE + MPE_PAYLOAD_SIZE)) / MPE_PAYLOAD_SIZE) + 2;
              }
              else {
                packet_count = 1;
              }
              if (produced == size) {
                break;
              }
            }
          }
        }
        if (packet_count != 0) {
          packet_count--;
          if (packet_count == 0) {
            /* last TS packet */
            packet = pcap_next(descr, &hdr);
            if (packet == NULL) {
              /* no pending IP packet */
              offset = 0;
              tsHeader.sync_byte = 0x47;
              tsHeader.transport_error_indicator = 0x0;
              tsHeader.payload_unit_start_indicator = 0x0;
              tsHeader.transport_priority = 0x1;
              tsHeader.pid_12to8 = ((pidMPE) >> 8) & 0x1f;
              tsHeader.pid_7to0 = (pidMPE) & 0xff;
              tsHeader.transport_scrambling_control = 0x0;
              tsHeader.adaptation_field_control = 0x1;
              tsHeader.continuity_counter = mpe_continuity_counter & 0xf;
              mpe_continuity_counter = (mpe_continuity_counter + 1) & 0xf;
              memcpy(&mpe[offset], (unsigned char *)&tsHeader, TS_HEADER_SIZE);
              offset += TS_HEADER_SIZE;

              if (shift < 3) {
                while (shift >= 0) {
                  mpe[offset++] = (crc32_partial >> (shift * 8)) & 0xff;
                  shift--;
                }
              }
              else {
                ptr = packet_ptr;
                for (int i = 0; i < packet_length; i++) {
                  mpe[offset++] = *ptr++;
                }

                crc32 = crc32_calc_final(packet_ptr, packet_length, crc32_partial);
                memcpy(&mpe[offset], (unsigned char *) &crc32, sizeof(crc32));
                offset += sizeof(crc32);
              }
            }
            else {
              /* pending IP packet */
              if ((packet_length + sizeof(crc32)) >= (MPE_PAYLOAD_PP_SIZE)) {
                /* pending IP packet header doesn't fit */
                next_packet_valid = TRUE;
                offset = 0;
                tsHeader.sync_byte = 0x47;
                tsHeader.transport_error_indicator = 0x0;
                tsHeader.payload_unit_start_indicator = 0x0;
                tsHeader.transport_priority = 0x1;
                tsHeader.pid_12to8 = ((pidMPE) >> 8) & 0x1f;
                tsHeader.pid_7to0 = (pidMPE) & 0xff;
                tsHeader.transport_scrambling_control = 0x0;
                tsHeader.adaptation_field_control = 0x1;
                tsHeader.continuity_counter = mpe_continuity_counter & 0xf;
                mpe_continuity_counter = (mpe_continuity_counter + 1) & 0xf;
                memcpy(&mpe[offset], (unsigned char *)&tsHeader, TS_HEADER_SIZE);
                offset += TS_HEADER_SIZE;

                if (shift < 3) {
                  while (shift >= 0) {
                    mpe[offset++] = (crc32_partial >> (shift * 8)) & 0xff;
                    shift--;
                  }
                }
                else {
                  ptr = packet_ptr;
                  for (int i = 0; i < packet_length; i++) {
                    mpe[offset++] = *ptr++;
                  }

                  crc32 = crc32_calc_final(packet_ptr, packet_length, crc32_partial);
                  memcpy(&mpe[offset], (unsigned char *) &crc32, sizeof(crc32));
                  offset += sizeof(crc32);
                }
              }
              else {
                /* pending IP packet header does fit */
                offset = 0;
                tsHeader.sync_byte = 0x47;
                tsHeader.transport_error_indicator = 0x0;
                tsHeader.payload_unit_start_indicator = 0x1;
                tsHeader.transport_priority = 0x1;
                tsHeader.pid_12to8 = ((pidMPE) >> 8) & 0x1f;
                tsHeader.pid_7to0 = (pidMPE) & 0xff;
                tsHeader.transport_scrambling_control = 0x0;
                tsHeader.adaptation_field_control = 0x1;
                tsHeader.continuity_counter = mpe_continuity_counter & 0xf;
                mpe_continuity_counter = (mpe_continuity_counter + 1) & 0xf;
                memcpy(&mpe[offset], (unsigned char *)&tsHeader, TS_HEADER_SIZE);
                offset += TS_HEADER_SIZE;

                if (shift < 3) {
                  mpe[offset++] = shift + 1;    /* Payload Pointer */
                  while (shift >= 0) {
                    mpe[offset++] = (crc32_partial >> (shift * 8)) & 0xff;
                    shift--;
                  }
                }
                else {
                  mpe[offset++] = packet_length + sizeof(crc32);    /* Payload Pointer */
                  ptr = packet_ptr;
                  for (int i = 0; i < packet_length; i++) {
                    mpe[offset++] = *ptr++;
                  }

                  crc32 = crc32_calc_final(packet_ptr, packet_length, crc32_partial);
                  memcpy(&mpe[offset], (unsigned char *) &crc32, sizeof(crc32));
                  offset += sizeof(crc32);
                }
                memcpy(packet_save, packet, hdr.len);
                temp_offset = offset;

                length = (hdr.len - sizeof(struct ether_header)) + (DATAGRAM_HEADER_SIZE - 3) + sizeof(crc32);

                datagram.table_id = 0x3e;   /* DSM-CC section */
                datagram.section_syntax_indicator = 1;
                datagram.private_indicator = 0;
                datagram.reserved0 = 0x3;
                datagram.section_length_h = (length >> 8) & 0xf;
                datagram.section_length_l = length & 0xff;
                datagram.reserved1 = 0x3;
                datagram.payload_scrambling_control = 0;
                datagram.address_scrambling_control = 0;
                datagram.LLC_SNAP_FLAG = 0;
                datagram.current_next_indicator = 1;
                datagram.section_number = 0;
                datagram.last_section_number = 0;
                eptr = (struct ether_header *)packet_save;
                ptr = eptr->ether_dhost;

                datagram.MAC_address_1 = *ptr++;
                datagram.MAC_address_2 = *ptr++;
                datagram.MAC_address_3 = *ptr++;
                datagram.MAC_address_4 = *ptr++;
                datagram.MAC_address_5 = *ptr++;
                datagram.MAC_address_6 = *ptr++;

                memcpy(&mpe[offset], (unsigned char *)&datagram, DATAGRAM_HEADER_SIZE);
                offset += DATAGRAM_HEADER_SIZE;

                if (ping_reply_mode) {
                  ping_reply();
                }
                if (ipaddr_spoof_mode) {
                  ipaddr_spoof();
                }

                remainder = MPEG2_PACKET_SIZE - offset;
                ptr = (unsigned char *)(packet_save + sizeof(struct ether_header));
                if ((hdr.len - sizeof(struct ether_header)) < remainder) {
                  for (unsigned int i = 0; i < hdr.len - sizeof(struct ether_header); i++) {
                    mpe[offset++] = *ptr++;
                  }
                }
                else {
                  for (unsigned int i = 0; i < remainder; i++) {
                    mpe[offset++] = *ptr++;
                  }
                }
                crc32_partial = crc32_calc_partial(&mpe[temp_offset], offset - temp_offset, 0xffffffff);
                packet_ptr = ptr;
                packet_length = hdr.len - sizeof(struct ether_header) + DATAGRAM_HEADER_SIZE - (offset - temp_offset);
                shift = 3;
                remainder = MPEG2_PACKET_SIZE - offset;
                if (remainder != 0) {
                  if (remainder >= 4) {
                    remainder = 4;
                    packet_count = 0;
                  }
                  else {
                    packet_count = 1;
                  }
                  for (unsigned int i = 0; i < remainder; i++) {
                    mpe[offset++] = (crc32_partial >> (shift * 8)) & 0xff;
                    shift--;
                  }
                }
                else {
                  if (hdr.len - sizeof(struct ether_header) + sizeof(crc32) + DATAGRAM_HEADER_SIZE > ((offset - temp_offset) + MPE_PAYLOAD_SIZE)) {
                    packet_count = ((hdr.len - sizeof(struct ether_header) + sizeof(crc32) + DATAGRAM_HEADER_SIZE - ((offset - temp_offset) + MPE_PAYLOAD_SIZE)) / MPE_PAYLOAD_SIZE) + 2;
                  }
                  else {
                    packet_count = 1;
                  }
                }
              }
            }
            memset(&mpe[offset], 0xff, MPEG2_PACKET_SIZE - offset);
            dump_packet();
            memcpy(&out[produced], &mpe[0], MPEG2_PACKET_SIZE);
            produced += MPEG2_PACKET_SIZE;
            if (produced == size) {
              break;
            }
          }
          else {
            /* middle packets */
            offset = 0;
            tsHeader.sync_byte = 0x47;
            tsHeader.transport_error_indicator = 0x0;
            tsHeader.payload_unit_start_indicator = 0x0;
            tsHeader.transport_priority = 0x1;
            tsHeader.pid_12to8 = ((pidMPE) >> 8) & 0x1f;
            tsHeader.pid_7to0 = (pidMPE) & 0xff;
            tsHeader.transport_scrambling_control = 0x0;
            tsHeader.adaptation_field_control = 0x1;
            tsHeader.continuity_counter = mpe_continuity_counter & 0xf;
            mpe_continuity_counter = (mpe_continuity_counter + 1) & 0xf;
            memcpy(&mpe[offset], (unsigned char *)&tsHeader, TS_HEADER_SIZE);
            offset += TS_HEADER_SIZE;

            ptr = packet_ptr;
            if (packet_length < MPE_PAYLOAD_SIZE) {
              for (int i = 0; i < packet_length; i++) {
                mpe[offset++] = *ptr++;
              }
              crc32_partial = crc32_calc_partial(packet_ptr, packet_length, crc32_partial);
              shift = 3;
              while (MPE_PAYLOAD_SIZE - packet_length) {
                mpe[offset++] = (crc32_partial >> (shift * 8)) & 0xff;
                packet_length++;
                shift--;
              }
              if (shift == -1) {
                packet_count = 0;
              }
              else {
                packet_length = 0;
              }
            }
            else {
              for (int i = 0; i < MPE_PAYLOAD_SIZE; i++) {
                mpe[offset++] = *ptr++;
              }
              crc32_partial = crc32_calc_partial(packet_ptr, MPE_PAYLOAD_SIZE, crc32_partial);
              packet_ptr += MPE_PAYLOAD_SIZE;
              packet_length -= MPE_PAYLOAD_SIZE;
            }
            dump_packet();

            memcpy(&out[produced], &mpe[0], MPEG2_PACKET_SIZE);
            produced += MPEG2_PACKET_SIZE;
            if (produced == size) {
              break;
            }
          }
        }
        else {
          memcpy(&out[produced], &stuffing[0], MPEG2_PACKET_SIZE);
          produced += MPEG2_PACKET_SIZE;
          if (produced == size) {
            break;
          }
        }
      }

      // Tell runtime system how many output items we produced.
      return noutput_items;
    }

  } /* namespace mpe */
} /* namespace gr */

