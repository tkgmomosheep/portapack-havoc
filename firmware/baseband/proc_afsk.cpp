/*
 * Copyright (C) 2014 Jared Boone, ShareBrained Technology, Inc.
 * Copyright (C) 2016 Furrtek
 *
 * This file is part of PortaPack.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#include "proc_afsk.hpp"
#include "portapack_shared_memory.hpp"
#include "sine_table_int8.hpp"
#include "event_m4.hpp"

#include <cstdint>

void AFSKProcessor::execute(const buffer_c8_t& buffer) {
	
	// This is called at 2280000/2048 = 1113Hz
	
	if (!configured) return;
	
	for (size_t i = 0; i<buffer.count; i++) {
		
		// Tone generation at 2280000/10 = 228kHz
		if (s >= (10 - 1)) {
			s = 0;
			
			if (sample_count >= afsk_samples_per_bit) {
				if (configured == true) {
					cur_byte = message_data[byte_pos];
					ext_byte = message_data[byte_pos + 1];
					if (!(cur_byte | ext_byte)) {
						if (repeat_counter < afsk_repeat) {
							bit_pos = 0;
							byte_pos = 0;
							cur_byte = message_data[0];
							ext_byte = message_data[1];
							message.n = repeat_counter + 1;
							shared_memory.application_queue.push(message);
							repeat_counter++;
						} else {
							message.n = 0;
							shared_memory.application_queue.push(message);
							cur_byte = 0;
							ext_byte = 0;
							configured = false;
						}
					}
				}
				
				if (!afsk_alt_format) {
					// 0bbbbbbbp1
					// Start, 7-bit data, parity, stop
					gbyte = 0;
					gbyte = cur_byte << 1;
					gbyte |= 1;
				} else {
					// 0bbbbbbbbp
					// Start, 8-bit data, parity
					gbyte = 0;
					gbyte = cur_byte << 1;
					gbyte |= (ext_byte & 1);
				}
				
				cur_bit = (gbyte >> (9 - bit_pos)) & 1;

				if (bit_pos == 9) {
					bit_pos = 0;
					if (!afsk_alt_format)
						byte_pos++;
					else
						byte_pos += 2;
				} else {
					bit_pos++;
				}
				
				sample_count = 0;
			} else {
				sample_count++;
			}
			if (cur_bit)
				tone_phase += afsk_phase_inc_mark;
			else
				tone_phase += afsk_phase_inc_space;
		} else {
			s++;
		}
		
		tone_sample = (sine_table_i8[(tone_phase & 0x03FC0000)>>18]); 
		
		// FM
		// 1<<18 = 262144
		// m = (262144 * BW) / 2280000 (* 115, see ui_lcr afsk_bw setting)
		frq = tone_sample * afsk_bw;
		
		phase = (phase + frq);
		sphase = phase + (64<<18);

		re = (sine_table_i8[(sphase & 0x03FC0000)>>18]);
		im = (sine_table_i8[(phase & 0x03FC0000)>>18]);
		
		buffer.p[i] = {(int8_t)re,(int8_t)im};
	}
}

void AFSKProcessor::on_message(const Message* const p) {
	const auto message = *reinterpret_cast<const AFSKConfigureMessage*>(p);
	if (message.id == Message::ID::AFSKConfigure) {
		memcpy(message_data, message.message_data, 512);
		afsk_samples_per_bit = message.afsk_samples_per_bit;
		afsk_phase_inc_mark = message.afsk_phase_inc_mark;
		afsk_phase_inc_space = message.afsk_phase_inc_space;
		afsk_repeat = message.afsk_repeat - 1;
		afsk_bw = message.afsk_bw;
		afsk_alt_format = message.afsk_alt_format;
	
		sample_count = afsk_samples_per_bit;
		repeat_counter = 0;
		bit_pos = 0;
		byte_pos = 0;
		cur_byte = 0;
		ext_byte = 0;
		cur_bit = 0;
		configured = true;
	}
}

int main() {
	EventDispatcher event_dispatcher { std::make_unique<AFSKProcessor>() };
	event_dispatcher.run();
	return 0;
}
