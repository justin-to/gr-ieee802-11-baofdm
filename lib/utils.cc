/*
 * Copyright (C) 2013 Bastian Bloessl <bloessl@ccs-labs.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "utils.h"

#include <cassert>
#include <cstring>
#include <math.h>

ofdm_param::ofdm_param(Encoding e) {
	encoding = e;

	switch(e) {
		case BPSK_1_2:
            // bits per data carrier
			n_bpsc = 1;
            // parity bits per ofdm symbol (I think)
			n_cbps = 24; // 24 when using 24 subs
            // data bits per ofdm symbol
			n_dbps = 12; // 12 when using 24 subs
            // data rate
            // this gets read out in reverse
			rate_field = 0x0D; // 0b00001101
			break;

		case BPSK_3_4:
			n_bpsc = 1;
			n_cbps = 48;
			n_dbps = 36;
			rate_field = 0x0F; // 0b00001111
			break;

		case QPSK_1_2:
			n_bpsc = 2;
			n_cbps = 96;
			n_dbps = 48;
			rate_field = 0x05; // 0b00000101
			break;

		case QPSK_3_4:
			n_bpsc = 2;
			n_cbps = 96;
			n_dbps = 72;
			rate_field = 0x07; // 0b00000111
			break;

		case QAM16_1_2:
			n_bpsc = 4;
			n_cbps = 192;
			n_dbps = 96;
			rate_field = 0x09; // 0b00001001
			break;

		case QAM16_3_4:
			n_bpsc = 4;
			n_cbps = 192;
			n_dbps = 144;
			rate_field = 0x0B; // 0b00001011
			break;

		case QAM64_2_3:
			n_bpsc = 6;
			n_cbps = 288;
			n_dbps = 192;
			rate_field = 0x01; // 0b00000001
			break;

		case QAM64_3_4:
			n_bpsc = 6;
			n_cbps = 288;
			n_dbps = 216;
			rate_field = 0x03; // 0b00000011
			break;

		case BPSK_1_2_HEADER:
            // bits per data carrier
			n_bpsc = 1;
            // parity bits per ofdm symbol (I think)
			n_cbps = 48;
            // data bits per ofdm symbol
			n_dbps = 24;
            // data rate
            // this gets read out in reverse
			rate_field = 0x0D; // 0b00001101
			break;

		defaut:
			assert(false);
			break;
	}
}


void
ofdm_param::print() {
	std::cout << "OFDM Parameters:" << std::endl;
	std::cout << "endcoding :" << encoding << std::endl;
	std::cout << "rate_field :" << (int)rate_field << std::endl;
	std::cout << "n_bpsc :" << n_bpsc << std::endl;
	std::cout << "n_cbps :" << n_cbps << std::endl;
	std::cout << "n_dbps :" << n_dbps << std::endl;
}


frame_param::frame_param(ofdm_param &ofdm, int psdu_length) {

	psdu_size = psdu_length;

	// number of symbols (17-11)
    // total number of symbols needed to transmit this frame
    // 16 service bits + PSDU bits + 6 tail bits
	n_sym = (int) ceil((16 + 8 * psdu_size + 6) / (double) ofdm.n_dbps);

	n_data_bits = n_sym * ofdm.n_dbps;

	// number of padding bits (17-13)
	n_pad = n_data_bits - (16 + 8 * psdu_size + 6);

	n_encoded_bits = n_sym * ofdm.n_cbps;
}
void
frame_param::print() {
	std::cout << "FRAME Parameters:" << std::endl;
	std::cout << "psdu_size: " << psdu_size << std::endl;
	std::cout << "n_sym: " << n_sym << std::endl;
	std::cout << "n_pad: " << n_pad << std::endl;
	std::cout << "n_encoded_bits: " << n_encoded_bits << std::endl;
	std::cout << "n_data_bits: " << n_data_bits << std::endl;
}


void scramble(const char *in, char *out, frame_param &frame, char initial_state) {

    int state = initial_state;
    int feedback;

    for (int i = 0; i < frame.n_data_bits; i++) {

	feedback = (!!(state & 64)) ^ (!!(state & 8));
	out[i] = feedback ^ in[i];
	state = ((state << 1) & 0x7e) | feedback;
    }
}

// zero out only the tail bits
void reset_tail_bits(char *scrambled_data, frame_param &frame) {
	memset(scrambled_data + frame.n_data_bits - frame.n_pad - 6, 0, 6 * sizeof(char));
}


int ones(int n) {
	int sum = 0;
	for(int i = 0; i < 8; i++) {
		if(n & (1 << i)) {
			sum++;
		}
	}
	return sum;
}


void convolutional_encoding(const char *in, char *out, frame_param &frame) {

	int state = 0;

	for(int i = 0; i < frame.n_data_bits; i++) {
		assert(in[i] == 0 || in[i] == 1);
		state = ((state << 1) & 0x7e) | in[i];
		out[i * 2]     = ones(state & 0155) % 2;
		out[i * 2 + 1] = ones(state & 0117) % 2;
	}
}


void puncturing(const char *in, char *out, frame_param &frame, ofdm_param &ofdm) {

	int mod;

	for (int i = 0; i < frame.n_data_bits * 2; i++) {
		switch(ofdm.encoding) {
			case BPSK_1_2:
			case BPSK_1_2_HEADER:
			case QPSK_1_2:
			case QAM16_1_2:
				*out = in[i];
				out++;
				break;

			case QAM64_2_3:
				if (i % 4 != 3) {
					*out = in[i];
					out++;
				}
				break;

			case BPSK_3_4:
			case QPSK_3_4:
			case QAM16_3_4:
			case QAM64_3_4:
				mod = i % 6;
				if (!(mod == 3 || mod == 4)) {
					*out = in[i];
					out++;
				}
				break;
			defaut:
				assert(false);
				break;
		}
	}
}


void interleave(const char *in, char *out, frame_param &frame, ofdm_param &ofdm, int num_subcarriers, bool reverse) {

	int n_cbps = ofdm.n_cbps;
	int first[n_cbps];
	int second[n_cbps];
	int s = std::max(ofdm.n_bpsc / 2, 1);
	// make sure to use nse numbers that are a multiple of 3, can make error check later
    int num_rows = num_subcarriers / 3;

	for(int j = 0; j < n_cbps; j++) {
		first[j] = s * (j / s) + ((j + int(floor(num_rows * j / n_cbps))) % s);
	}

	for(int i = 0; i < n_cbps; i++) {
		second[i] = num_rows * i - (n_cbps - 1) * int(floor(num_rows * i / n_cbps));
	}

	for(int i = 0; i < frame.n_sym; i++) {
		for(int k = 0; k < n_cbps; k++) {
			if(reverse) {
				out[i * n_cbps + second[first[k]]] = in[i * n_cbps + k];
			} else {
				out[i * n_cbps + k] = in[i * n_cbps + second[first[k]]];
			}
		}
	}
}

// repack bits into data symbols
void split_symbols(const char *in, char *out, frame_param &frame, ofdm_param &ofdm, int num_subcarriers) {

    // number of OFDM symbols * number of subcarriers = number of data symbols
	int symbols = frame.n_sym * num_subcarriers;

	for (int i = 0; i < symbols; i++) {
		out[i] = 0;
		for(int k = 0; k < ofdm.n_bpsc; k++) {
			assert(*in == 1 || *in == 0);
			out[i] |= (*in << k);
			in++;
		}
	}
}


void generate_bits(const char *psdu, char *data_bits, frame_param &frame) {

	// first 16 bits are zero (SERVICE/DATA field)
	memset(data_bits, 0, 16);
	data_bits += 16;

	for(int i = 0; i < frame.psdu_size; i++) {
		for(int b = 0; b < 8; b++) {
			data_bits[i * 8 + b] = !!(psdu[i] & (1 << b));
		}
	}
}
