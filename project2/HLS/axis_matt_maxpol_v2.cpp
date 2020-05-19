#include <ap_int.h>
#include <ap_fixed.h>
#include <hls_stream.h>

typedef ap_fixed<16,8> datai_t;
typedef ap_fixed<32,16> datao_t;
typedef ap_fixed<16,8> op_t;
typedef ap_fixed<32,16> mul_t;
typedef ap_fixed<32,16> acc_t;

struct ap_i_axis{
    datai_t data;
    ap_uint<1> last;
};
struct ap_o_axis{
    datao_t  data;
    ap_uint<1> last;
};

// The top-level function
void axis_matt_mult_maxpool_v2(
    hls::stream<ap_o_axis> &strm_out,
    hls::stream<ap_i_axis> &strm_in
    )
{
#pragma HLS INTERFACE ap_ctrl_none port=return
#pragma HLS interface axis port=strm_in
#pragma HLS INTERFACE axis port=strm_out

    struct ap_i_axis tmp;
    struct ap_o_axis tmpa;
    static op_t op1, op2;
    static mul_t mult;
    static acc_t acc;
    static acc_t max;
    static op_t bias;
    static datai_t matB[1024];
    static datai_t matA[16384];
    int i, j, k, n, m, z, index;

	// save 22 bias + 22*25 matrixes kernel
    for (i=0; i<1024; i++) {
	   tmp = strm_in.read();
	   matB[i] = tmp.data;
	   if (tmp.last == 1) break ;
    }

	// save 574x25 matrix A
    for(i=0; i<16384; i++){
	   tmp = strm_in.read();
	   matA[i] = tmp.data;
	   if(tmp.last == 1) break;
    }

    for(i=0; i<22; i++){
	    bias = matB[i];
	    for(j=0; j<12; j++){
		    for(z=0; z<12; z++){
		 	    for(m=0; m<2; m++){
				    for(n=0; n<2; n++){
					    acc=0.0;
					    index = (j*2 + m)*24*25 + (z*2 + n)*25;
					    for(k=0; k<25; k++){
						    op1 = matB[22+i*25+k];
						    op2 = matA[index+k];
						    mult = op1*op2;
						    acc+=mult;
					    }
					    acc+=bias;
					    if(m==0 && n==0){
						    max = acc;
					    }
					    else if(max < acc){
						    max = acc;
					    }
				    }
			    }
			    if(i==21 && j==11 && z==11){
				    tmpa.last = 1;
			    }
			    else {
				    tmpa.last = 0;
			    }
			    tmpa.data = (datao_t)max;
			    strm_out.write(tmpa);
		    }
	    }
    }
}
