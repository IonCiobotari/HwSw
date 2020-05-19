#include <ap_int.h>
#include <hls_stream.h>

struct ap_axis{
  ap_int<32> data;
  ap_uint<1> last;
};

// The top-level function
void axis_macc_opt(
      hls::stream<ap_axis> &strm_out,
      hls::stream<ap_axis> &strm_in
      )
{
#pragma HLS INTERFACE ap_ctrl_none port=return
#pragma HLS interface axis port=strm_in
#pragma HLS INTERFACE axis port=strm_out

   struct ap_axis tmp, tmpa;
   static ap_int<64> mult;
   static ap_int<66> acc;
   static ap_uint<9> vect_size;
   static ap_int<8> localmem[512];
   int i, j, z;

   for(i = 0; i < 512; ){
	  tmp = strm_in.read();
	  for(j=0; j < 4; j++, i++){
		  localmem[i]=tmp.data.range(7+j*8, j*8);
	  }
	  if(tmp.last == 1) break;
   }

   acc = 0;
   i = 0;
   j=0;
   for (; ; ) {
#pragma HLS pipeline
	   tmp = strm_in.read();

	   for(z=0 ; z < 4; z++){
		   mult = tmp.data.range(7+z*8, z*8) * localmem[i];
		   i++;
		   acc += mult;
		   if(i==25){
			   tmpa.data.range(7+j*8, j*8) = acc;
			   j++;
			   if(j == 4){
				   tmpa.last = tmp.last;
			   	   strm_out.write(tmpa);
			   	   j=0;
			   }
			   i=0;
			   acc=0;
		   }
	   }
	   if(tmp.last==1) break;
   }
   if(j != 0){
	   tmpa.data.range(31, j*8) = 0;
	   tmpa.last = tmp.last;
	   strm_out.write(tmpa);
   }
}
