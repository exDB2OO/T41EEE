
#include "SDT.h"
#define ARM_MATH_CM7

char atom, currentAtom;
int8_t first_block = 1;
uint8_t NB_on = 0;
uint8_t wait_flag;
float32_t audiotmp = 0.0f;
//float32_t audioSpectBuffer[1024];  // This can't be DMAMEM.  It will break the S-Meter.  KF5N October 10, 2023
float32_t* audioSpectBuffer = new float32_t[1024];  // Assign to the heap.
float32_t sample_meanL = 0.0;
float32_t sample_meanR = 0.0;
float32_t wold = 0.0f;

//=== CW Filter ===
float32_t CW_AudioFilter1_state[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };  // AFP 10-18-22
float32_t CW_AudioFilter2_state[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };  // AFP 10-18-22
float32_t CW_AudioFilter3_state[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };  // AFP 10-18-22
float32_t CW_AudioFilter4_state[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };  // AFP 10-18-22
float32_t CW_AudioFilter5_state[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };  // AFP 10-18-22
//---------  Code Filter instance -----
arm_biquad_cascade_df2T_instance_f32 S1_CW_AudioFilter1 = { 6, CW_AudioFilter1_state, CW_AudioFilterCoeffs1 };  // AFP 10-18-22
arm_biquad_cascade_df2T_instance_f32 S1_CW_AudioFilter2 = { 6, CW_AudioFilter2_state, CW_AudioFilterCoeffs2 };  // AFP 10-18-22
arm_biquad_cascade_df2T_instance_f32 S1_CW_AudioFilter3 = { 6, CW_AudioFilter3_state, CW_AudioFilterCoeffs3 };  // AFP 10-18-22
arm_biquad_cascade_df2T_instance_f32 S1_CW_AudioFilter4 = { 6, CW_AudioFilter4_state, CW_AudioFilterCoeffs4 };  // AFP 10-18-22
arm_biquad_cascade_df2T_instance_f32 S1_CW_AudioFilter5 = { 6, CW_AudioFilter5_state, CW_AudioFilterCoeffs5 };  // AFP 10-18-22
//=== end CW Filter ===

/*****
  Purpose: Read audio from Teensy Audio Library
             Calculate FFT for display
             Process audio into SSB signal
             Output audio to amplifier

   Parameter List:
      void

   Return value:
      void
 *****/
void ProcessIQData()
{
  if (keyPressedOn == 1) { //AFP 09-01-22
    return;
  }
  /**********************************************************************************  AFP 12-31-20
        Get samples from queue buffers
        Teensy Audio Library stores ADC data in two buffers size=128, Q_in_L and Q_in_R as initiated from the audio lib.
        Then the buffers are read into two arrays sp_L and sp_R in blocks of 128 up to N_BLOCKS.  The arrarys are
        of size BUFFER_SIZE * N_BLOCKS.  BUFFER_SIZE is 128.
        N_BLOCKS = FFT_LENGTH / 2 / BUFFER_SIZE * (uint32_t)DF; // should be 16 with DF == 8 and FFT_LENGTH = 512
        BUFFER_SIZE*N_BLOCKS = 2024 samples
     **********************************************************************************/
  float32_t audioMaxSquared;
  uint32_t AudioMaxIndex;
  float rfGainValue;
  int rfGain;
//  float dataMax; 
//  uint32_t dataMaxIndex;

  // Are there at least N_BLOCKS buffers in each channel available ?  N_BLOCKS should be 16.
  if ( (uint32_t) Q_in_L.available() > N_BLOCKS && (uint32_t) Q_in_R.available() > N_BLOCKS ) {     // Removed addition of 0 to N_BLOCKS.
    usec = 0;
    // Get audio samples from the audio  buffers and convert them to float.
    // Read in 16 blocks and 128 samples in I and Q.  16 * 128 = 2048  (N_BLOCKS = 16)
    for (unsigned i = 0; i < N_BLOCKS; i++) {
//      sp_L1 = Q_in_R.readBuffer();      // Teensy Audio component:  AudioRecordQueue, part of receiver.
//      sp_R1 = Q_in_L.readBuffer();      // Why does R go into L and L go into R???
      // Find the maximum value and record.
    //  void arm_absmax_q15	(	const q15_t * 	pSrc, uint32_t 	blockSize, q15_t * 	pResult, uint32_t * 	pIndex);
      /**********************************************************************************  AFP 12-31-20
          Using arm_Math library, convert to float one buffer_size.
          Float_buffer samples are now standardized from > -1.0 to < 1.0
      **********************************************************************************/
      arm_q15_to_float(Q_in_R.readBuffer(), &float_buffer_L[BUFFER_SIZE * i], BUFFER_SIZE); // convert int_buffer to float 32bit.  BUFFER_SIZE = 128.
      arm_q15_to_float(Q_in_L.readBuffer(), &float_buffer_R[BUFFER_SIZE * i], BUFFER_SIZE); // convert int_buffer to float 32bit
      Q_in_L.freeBuffer();
      Q_in_R.freeBuffer();
    }  // end for loop

    if (keyPressedOn == 1) { //AFP 09-01-22.  Bail out if transmitting.
      return;
    }
    // Set frequency here only to minimize interruption to signal stream during tuning.
    // This code was unnecessary in the revised tuning scheme.  KF5N July 22, 2023
    if (centerTuneFlag == 1) { //AFP 10-04-22
      DrawBandWidthIndicatorBar();
      ShowFrequency();
    }                       //AFP 10-04-22
    centerTuneFlag = 0;     //AFP 10-04-22
    if (resetTuningFlag == 1) {
      ResetTuning();
    }
    resetTuningFlag = 0;

    //  Set RFGain for all bands.
    if(EEPROMData.autoGain) rfGain = EEPROMData.rfGainCurrent;   // Auto-gain
       else rfGain = EEPROMData.rfGain[EEPROMData.currentBand];  // Manual gain adjust.
    rfGainValue = pow(10, (float)rfGain / 20) * 4.0;  // KF5N January 16 2024
    arm_scale_f32 (float_buffer_L, rfGainValue, float_buffer_L, BUFFER_SIZE * N_BLOCKS); //AFP 09-27-22
    arm_scale_f32 (float_buffer_R, rfGainValue, float_buffer_R, BUFFER_SIZE * N_BLOCKS); //AFP 09-27-22
    arm_clip_f32	(float_buffer_L, float_buffer_L, -1.0, 1.0, 2048);
    arm_clip_f32	(float_buffer_R, float_buffer_R, -1.0, 1.0, 2048);
//    arm_max_f32(float_buffer_L, 2048, &dataMax, &dataMaxIndex);
//    Serial.printf("dataMax = %f\n", dataMax);
    /**********************************************************************************  AFP 12-31-20
        Remove DC offset to reduce central spike.  First read the Mean value of
        left and right channels.  Then fill L and R correction arrays with those Means
        and subtract the Means from the float L and R buffer data arrays.  Again use Arm_Math functions
        to manipulate the arrays.  Arrays are all BUFFER_SIZE * N_BLOCKS long
    **********************************************************************************/

    /*arm_mean_f32(float_buffer_L, BUFFER_SIZE * N_BLOCKS, &sample_meanL);
    arm_mean_f32(float_buffer_R, BUFFER_SIZE * N_BLOCKS, &sample_meanR);

    for (uint32_t j = 0; j < BUFFER_SIZE * N_BLOCKS  ; j++) {
      L_BufferOffset [j] = -sample_meanL;
      R_BufferOffset [j] = -sample_meanR;
    }
    arm_add_f32(float_buffer_L , L_BufferOffset, float_buffer_L2 , BUFFER_SIZE * N_BLOCKS ) ;
    arm_add_f32(float_buffer_R , R_BufferOffset, float_buffer_R2 , BUFFER_SIZE * N_BLOCKS ) ;

    arm_biquad_cascade_df2T_f32(&s1_Receive, float_buffer_L, float_buffer_L, 2048); //AFP 09-23-22
    arm_biquad_cascade_df2T_f32(&s1_Receive, float_buffer_R, float_buffer_R, 2048); //AFP 09-23-22*/
    arm_biquad_cascade_df2T_f32(&s1_Receive2, float_buffer_L, float_buffer_L, 2048); //AFP 11-03-22
    arm_biquad_cascade_df2T_f32(&s1_Receive2, float_buffer_R, float_buffer_R, 2048); //AFP 11-03-22

    /**********************************************************************************  AFP 12-31-20
        Scale the data buffers by the RFgain value defined in bands[EEPROMData.currentBand] structure
    **********************************************************************************/
    // This gain scaling is not necessary.  It is assumed that Auto-gain and AGC will appropriately scale the incoming signal data.
//    arm_scale_f32 (float_buffer_L, bands[EEPROMData.currentBand].RFgain, float_buffer_L, BUFFER_SIZE * N_BLOCKS); //AFP 09-23-22
//    arm_scale_f32 (float_buffer_R, bands[EEPROMData.currentBand].RFgain, float_buffer_R, BUFFER_SIZE * N_BLOCKS); //AFP 09-23-22

    /**********************************************************************************  AFP 12-31-20
      Clear Buffers
      This is to prevent overfilled queue buffers during each switching event.
      (band change, mode change, frequency change, the audio chain runs and fills the buffers.
      If the buffers are full, the Teensy needs much more time.
      In that case, we clear the buffers to keep the whole audio chain running smoothly.
      **********************************************************************************/
    if (Q_in_L.available() > 25) {
      Q_in_L.clear();
    //  n_clear++; // just for debugging to check how often this occurs
      AudioInterrupts();
    }
    if (Q_in_R.available() > 25) {
      Q_in_R.clear();
    //  n_clear++; // just for debugging to check how often this occurs
      AudioInterrupts();
    }
    /**********************************************************************************  AFP 12-31-20
      IQ amplitude and phase correction.  For this scaled down version the I an Q chnnels are
      equalized and phase corrected manually. This is done by applying a correction, which is the difference, to
      the L channel only.  The phase is corrected in the IQPhaseCorrection() function.
    ***********************************************************************************************/

    // Manual IQ amplitude correction
    // to be honest: we only correct the amplitude of the I channel ;-)
    if (bands[EEPROMData.currentBand].mode == DEMOD_LSB || bands[EEPROMData.currentBand].mode == DEMOD_AM || bands[EEPROMData.currentBand].mode == DEMOD_SAM) {
      arm_scale_f32 (float_buffer_L, -EEPROMData.IQAmpCorrectionFactor[EEPROMData.currentBand], float_buffer_L, BUFFER_SIZE * N_BLOCKS); //AFP 04-14-22
      IQPhaseCorrection(float_buffer_L, float_buffer_R, EEPROMData.IQPhaseCorrectionFactor[EEPROMData.currentBand], BUFFER_SIZE * N_BLOCKS);
    } else {
      if (bands[EEPROMData.currentBand].mode == DEMOD_USB || bands[EEPROMData.currentBand].mode == DEMOD_AM || bands[EEPROMData.currentBand].mode == DEMOD_SAM) {
        arm_scale_f32 (float_buffer_L, -EEPROMData.IQAmpCorrectionFactor[EEPROMData.currentBand], float_buffer_L, BUFFER_SIZE * N_BLOCKS); //AFP 04-14-22
        IQPhaseCorrection(float_buffer_L, float_buffer_R, EEPROMData.IQPhaseCorrectionFactor[EEPROMData.currentBand], BUFFER_SIZE * N_BLOCKS);
      }
    }

    display_S_meter_or_spectrum_state++;
    if ( keyPressedOn == 1) { ////AFP 09-01-22.  Is this a duplicate here???
      return;
    }

    /**********************************************************************************  AFP 12-31-20
        Frequency translation by Fs/4 without multiplication from Lyons (2011): chapter 13.1.2 page 646
        together with the savings of not having to shift/rotate the FFT_buffer, this saves
        about 1% of processor use.

        This is for +Fs/4 [moves receive frequency to the left in the spectrum display]
           float_buffer_L contains I = real values
           float_buffer_R contains Q = imaginary values
           xnew(0) =  xreal(0) + jximag(0)
               leave first value (DC component) as it is!
           xnew(1) =  - ximag(1) + jxreal(1)
    **********************************************************************************/
    // X1 zoom must be done before the frequency shift!
    if ((EEPROMData.spectrum_zoom == 0) && (updateDisplayCounter == 1)) {
      updateDisplayFlag = 1;
      CalcZoom1Magn();
    }

    FreqShift1();

    /**********************************************************************************  AFP 12-31-20
        EEPROMData.spectrum_zoom_2 and larger here after frequency conversion!
        Spectrum zoom displays a magnified display of the data around the translated receive frequency.
        Processing is done in the ZoomFFTExe(BUFFER_SIZE * N_BLOCKS) function.  For magnifications of 2x to 16X
        Larger magnifications are possible.

        Spectrum Zoom uses the shifted spectrum, so the center "hump" around DC is shifted by fs/4
    **********************************************************************************/

    if((EEPROMData.spectrum_zoom == 1) && (updateDisplayCounter == 1))  ZoomFFTExe(BUFFER_SIZE * N_BLOCKS);
    if((EEPROMData.spectrum_zoom == 2) && (updateDisplayCounter < 2))  ZoomFFTExe(BUFFER_SIZE * N_BLOCKS);
    if((EEPROMData.spectrum_zoom == 3) && (updateDisplayCounter < 4))  ZoomFFTExe(BUFFER_SIZE * N_BLOCKS);
    if((EEPROMData.spectrum_zoom == 4) && (updateDisplayCounter < 8))  ZoomFFTExe(BUFFER_SIZE * N_BLOCKS);

    /**********************************************************************************  AFP 12-31-20
        S-Meter & dBm-display ?? not usually called
     **********************************************************************************/
    //============================== AFP 10-22-22  Begin new
    if (calibrateFlag == 1) {
      CalibrateOptions();
    }

    /*************************************************************************************************
        freq_conv2()

        FREQUENCY CONVERSION USING A SOFTWARE QUADRATURE OSCILLATOR
        Creates a new IF frequency to allow the tuning window to be moved anywhere in the current display.
        THIS VERSION calculates the COS AND SIN WAVE on the fly - uses double precision float

        MAJOR ADVANTAGE: frequency conversion can be done for any frequency !

        large parts of the code taken from the mcHF code by Clint, KA7OEI, thank you!
          see here for more info on quadrature oscillators:
        Wheatley, M. (2011): CuteSDR Technical Manual Ver. 1.01. - http://sourceforge.net/projects/cutesdr/
        Lyons, R.G. (2011): Understanding Digital Processing. – Pearson, 3rd edition.
     *************************************************************************************************/

    FreqShift2();  //AFP 12-14-21

    /**********************************************************************************  AFP 12-31-20
        Decimation
        Resample (Decimate) the shifted time signal, first by 4, then by 2.  Each time the
        signal is decimated by an even number, the spectrum is reversed.  Resampling twice
        returns the spectrum to the correct orientation.
        Signal has now been shifted to base band, leaving aliases at higher frequencies,
        which are removed at each decimation step using the Arm combined decimate/filter function.
        If the statring sample rate is 192K SPS after the combined decimation, the sample rate is
        now 192K/8 = 24K SPS.  The array size is also reduced by 8, making FFT calculations much faster.
        The effective bandwidth (up to Nyquist frequency) is 12KHz.
     **********************************************************************************/
    // decimation-by-4 in-place!
    arm_fir_decimate_f32(&FIR_dec1_I, float_buffer_L, float_buffer_L, BUFFER_SIZE * N_BLOCKS);
    arm_fir_decimate_f32(&FIR_dec1_Q, float_buffer_R, float_buffer_R, BUFFER_SIZE * N_BLOCKS);

    // decimation-by-2 in-place
    arm_fir_decimate_f32(&FIR_dec2_I, float_buffer_L, float_buffer_L, BUFFER_SIZE * N_BLOCKS / (uint32_t)DF1);
    arm_fir_decimate_f32(&FIR_dec2_Q, float_buffer_R, float_buffer_R, BUFFER_SIZE * N_BLOCKS / (uint32_t)DF1);

    // =================  AFP 10-21-22 Level Adjust ===========
    float freqKHzFcut;
    float volScaleFactor;
    if (bands[EEPROMData.currentBand].mode == DEMOD_LSB) {
      freqKHzFcut = -(float32_t)bands[EEPROMData.currentBand].FLoCut * 0.001;
    } else {
      freqKHzFcut = (float32_t)bands[EEPROMData.currentBand].FHiCut * 0.001;
    }
    volScaleFactor = 7.0874 * pow(freqKHzFcut, -1.232);
    arm_scale_f32(float_buffer_L, volScaleFactor, float_buffer_L, FFT_length / 2);
    arm_scale_f32(float_buffer_R, volScaleFactor, float_buffer_R, FFT_length / 2);

    /**********************************************************************************  AFP 12-31-20
        Digital FFT convolution
        Filtering is accomplished by combining (multiplying) spectra in the frequency domain.
         Basis for this was Lyons, R. (2011): Understanding Digital Processing.
         "Fast FIR Filtering using the FFT", pages 688 - 694.
         Method used here: overlap-and-save.

        First, Create Complex time signal for CFFT routine.
        Fill first block with Zeros.
        Then interleave RE and IM parts to create signal for FFT.
     **********************************************************************************/
    // Prepare the audio signal buffers:
    // ONLY FOR the VERY FIRST FFT: fill first samples with zeros

    if (first_block) { // Fill real & imaginaries with zeros for the first BLOCKSIZE samples.
      for (unsigned i = 0; i < BUFFER_SIZE * N_BLOCKS / (uint32_t)(DF / 2.0); i++) {
        FFT_buffer[i] = 0.0;
      }
      first_block = 0;
    } else  // All other FFTs

      // Fill FFT_buffer with last events audio samples for all other FFT instances.
      for (unsigned i = 0; i < BUFFER_SIZE * N_BLOCKS / (uint32_t)(DF); i++) {
        FFT_buffer[i * 2] = last_sample_buffer_L[i]; // real
        FFT_buffer[i * 2 + 1] = last_sample_buffer_R[i]; // imaginary
      }

    for (unsigned i = 0; i < BUFFER_SIZE * N_BLOCKS / (uint32_t)(DF); i++) {   // Copy recent samples to last_sample_buffer for next time!
      last_sample_buffer_L [i] = float_buffer_L[i];
      last_sample_buffer_R [i] = float_buffer_R[i];
    }

    //------------------------------ now fill recent audio samples into FFT_buffer (left channel: re, right channel: im)
    for (unsigned i = 0; i < BUFFER_SIZE * N_BLOCKS / (uint32_t)(DF); i++) {
      FFT_buffer[FFT_length + i * 2] = float_buffer_L[i]; // real
      FFT_buffer[FFT_length + i * 2 + 1] = float_buffer_R[i]; // imaginary
    }

    /**********************************************************************************  AFP 12-31-20
       Perform complex FFT on the audio time signals
       calculation is performed in-place the FFT_buffer [re, im, re, im, re, im . . .]
     **********************************************************************************/
    arm_cfft_f32(S, FFT_buffer, 0, 1);

    /**********************************************************************************  AFP 12-31-20
      Continuing FFT Convolution
          Next, prepare the filter mask (done in the Filter.cpp file).  Only need to do this once for each filter setting.
          Allows efficient real-time variable LP and HP audio filters, without the overhead of time-domain convolution filtering.

          After the Filter mask in the frequency domain is created, complex multiply  filter mask with the frequency domain audio data.
          Filter mask previously calculated in setup Array of filter mask coefficients:
          FIR_filter_mask[]
     **********************************************************************************/

    arm_cmplx_mult_cmplx_f32 (FFT_buffer, FIR_filter_mask, iFFT_buffer, FFT_length);
    if (updateDisplayFlag == 1) {
      for (int k = 0; k < 1024; k++) {
        audioSpectBuffer[1024 - k] = (iFFT_buffer[k] * iFFT_buffer[k]);
      }
      for (int k = 0; k < 256; k++) {
        if (bands[EEPROMData.currentBand].mode == 0  || bands[EEPROMData.currentBand].mode == DEMOD_AM || bands[EEPROMData.currentBand].mode == DEMOD_SAM) {  //AFP 10-26-22
          //audioYPixel[k] = 20+  map((int)displayScale[EEPROMData.currentScale].dBScale * log10f((audioSpectBuffer[1024 - k] + audioSpectBuffer[1024 - k + 1] + audioSpectBuffer[1024 - k + 2]) / 3), 0, 100, 0, 120);
          audioYPixel[k] = 50 +  map(15 * log10f((audioSpectBuffer[1024 - k] + audioSpectBuffer[1024 - k + 1] + audioSpectBuffer[1024 - k + 2]) / 3), 0, 100, 0, 120);
        }
        else if (bands[EEPROMData.currentBand].mode == 1) {//AFP 10-26-22
          //audioYPixel[k] = 20+   map((int)displayScale[EEPROMData.currentScale].dBScale * log10f((audioSpectBuffer[k] + audioSpectBuffer[k + 1] + audioSpectBuffer[k + 2]) / 3), 0, 100, 0, 120);
          audioYPixel[k] = 50 +   map(15 * log10f((audioSpectBuffer[k] + audioSpectBuffer[k + 1] + audioSpectBuffer[k + 2]) / 3), 0, 100, 0, 120);
        }
        if (audioYPixel[k] < 0)
          audioYPixel[k] = 0;
      }
      arm_max_f32 (audioSpectBuffer, 1024, &audioMaxSquared, &AudioMaxIndex);  // AFP 09-18-22 Max value of squared abin magnitued in audio
      audioMaxSquaredAve = .5 * audioMaxSquared + .5 * audioMaxSquaredAve;  //AFP 09-18-22Running averaged values
      DisplaydbM();
      DisplayAGC();
    }

    /**********************************************************************************
          Additional Convolution Processes:
              // filter by just deleting bins - principle of Linrad
      only works properly when we have the right window function!

        (automatic) notch filter = Tone killer --> the name is stolen from SNR ;-)
        first test, we set a notch filter at 1kHz
        which bin is that?
        positive & negative frequency -1kHz and +1kHz --> delete 2 bins
        we are not deleting one bin, but five bins for the test
        1024 bins in 12ksps = 11.71Hz per bin
        SR[SampleRate].rate / 8.0 / 1024 = bin BW
        1000Hz / 11.71Hz = bin 85.333

     **********************************************************************************/

    /**********************************************************************************  AFP 12-31-20
      After the frequency domain filter mask and other processes are complete, do a
      complex inverse FFT to return to the time domain
        (if sample rate = 192kHz, we are in 24ksps now, because we decimated by 8)
        perform iFFT (in-place)  IFFT is selected by the IFFT flag=1 in the Arm CFFT function.
     **********************************************************************************/

    arm_cfft_f32(iS, iFFT_buffer, 1, 1);

    // Adjust for level alteration because of filters.

    /**********************************************************************************  AFP 12-31-20
        AGC - automatic gain control

        we´re back in time domain
        AGC acts upon I & Q before demodulation on the decimated audio data in iFFT_buffer
     **********************************************************************************/
    AGC();  //AGC function works with time domain I and Q data buffers created in the last step.

    /**********************************************************************************
          Demodulation
            our time domain output is a combination of the real part (left channel) AND the imaginary part (right channel) of the second half of the FFT_buffer
            The demod mode is accomplished by selecting/combining the real and imaginary parts of the output of the IFFT process.
       **********************************************************************************/
    //===================== AFP 10-27-22  =========

    switch (bands[EEPROMData.currentBand].mode) {
      case DEMOD_LSB :
        for (unsigned i = 0; i < FFT_length / 2; i++) {
          //if (bands[EEPROMData.currentBand].mode == DEMOD_USB || bands[EEPROMData.currentBand].mode == DEMOD_LSB ) {  // for SSB copy real part in both outputs
          float_buffer_L[i] = iFFT_buffer[FFT_length + (i * 2)];

          float_buffer_R[i] = float_buffer_L[i];
          //}
        }
        break;
      case DEMOD_USB :
        for (unsigned i = 0; i < FFT_length / 2; i++) {
          // if (bands[EEPROMData.currentBand].mode == DEMOD_USB || bands[EEPROMData.currentBand].mode == DEMOD_LSB ) {  // for SSB copy real part in both outputs
          float_buffer_L[i] = iFFT_buffer[FFT_length + (i * 2)];

          float_buffer_R[i] = float_buffer_L[i];
          audiotmp = AlphaBetaMag(iFFT_buffer[FFT_length + (i * 2)], iFFT_buffer[FFT_length + (i * 2) + 1]);
          //}
        }

        break;
      case DEMOD_AM :
        for (unsigned i = 0; i < FFT_length / 2; i++) {     // Magnitude estimation Lyons (2011): page 652 / libcsdr
          audiotmp = AlphaBetaMag(iFFT_buffer[FFT_length + (i * 2)], iFFT_buffer[FFT_length + (i * 2) + 1]);
          // DC removal filter -----------------------
          w = audiotmp + wold * 0.99f; // Response to below 200Hz AFP 10-30-22
          float_buffer_L[i] = w - wold;
          wold = w;
        }
        arm_biquad_cascade_df1_f32 (&biquad_lowpass1, float_buffer_L, float_buffer_R, FFT_length / 2);
        arm_copy_f32(float_buffer_R, float_buffer_L, FFT_length / 2);
        break;
        case DEMOD_SAM : //AFP 11-03-22
        AMDecodeSAM();
        break;       
    }
    

    //============================  Receive EQ  ========================  AFP 08-08-22
    if (EEPROMData.receiveEQFlag == ON ) {
      DoReceiveEQ();
      arm_copy_f32(float_buffer_L, float_buffer_R, FFT_length / 2);
    }
    //============================ End Receive EQ

    /**********************************************************************************
      Noise Reduction
      3 algorithms working 3-15-22
      NR_Kim
      Spectral NR
      LMS variable leak NR
    **********************************************************************************/
    switch (NR_Index) {
      case 0:                               // NR Off
        break;
      case 1:                               // Kim NR
        Kim1_NR();
        arm_scale_f32 (float_buffer_L, 3, float_buffer_L, FFT_length / 2);  // Scalin factor reduced; was blasting speaker.  KF5N February 20, 2024.
        arm_scale_f32 (float_buffer_R, 3, float_buffer_R, FFT_length / 2);
        break;
      case 2:                               // Spectral NR
        SpectralNoiseReduction();
        break;
      case 3:                               // LMS NR
        ANR_notch = 0;
        Xanr();
        arm_scale_f32 (float_buffer_L, 1.5, float_buffer_L, FFT_length / 2);
        arm_scale_f32 (float_buffer_R, 2, float_buffer_R, FFT_length / 2);
        break;

    }
    //==================  End NR ============================
    // ===========================Automatic Notch ==================
    if (ANR_notchOn == 1) {
      ANR_notch = 1;
      Xanr();
      arm_copy_f32(float_buffer_R, float_buffer_L, FFT_length / 2);  //AFP 10-21-22
    }
    // ====================End notch =================================
    /**********************************************************************************
      EXPERIMENTAL: noise blanker
      by Michael Wild
    **********************************************************************************/

    if (NB_on != 0) {     
     NoiseBlanker(float_buffer_L, float_buffer_R);
      arm_copy_f32(float_buffer_R, float_buffer_L, FFT_length / 2);
    }
 
    if (T41State == CW_RECEIVE) {
      DoCWReceiveProcessing(); //AFP 09-19-22

      // ----------------------  CW Narrow band filters  AFP 10-18-22 -------------------------
      if (EEPROMData.CWFilterIndex != 5) {
        switch (EEPROMData.CWFilterIndex) {
          case 0:  // 0.8 KHz
            arm_biquad_cascade_df2T_f32(&S1_CW_AudioFilter1, float_buffer_L, float_buffer_L_AudioCW, 256);//AFP 10-18-22
            arm_copy_f32(float_buffer_L_AudioCW, float_buffer_L, FFT_length / 2);                         //AFP 10-18-22
            arm_copy_f32(float_buffer_L_AudioCW, float_buffer_R, FFT_length / 2);
            break;
          case 1: // 1.0 KHz
            arm_biquad_cascade_df2T_f32(&S1_CW_AudioFilter2, float_buffer_L, float_buffer_L_AudioCW, 256);//AFP 10-18-22
            arm_copy_f32(float_buffer_L_AudioCW, float_buffer_L, FFT_length / 2);                         //AFP 10-18-22
            arm_copy_f32(float_buffer_L_AudioCW, float_buffer_R, FFT_length / 2);
            break;
          case 2: // 1.3 KHz
            arm_biquad_cascade_df2T_f32(&S1_CW_AudioFilter3, float_buffer_L, float_buffer_L_AudioCW, 256);//AFP 10-18-22
            arm_copy_f32(float_buffer_L_AudioCW, float_buffer_L, FFT_length / 2);                         //AFP 10-18-22
            arm_copy_f32(float_buffer_L_AudioCW, float_buffer_R, FFT_length / 2);
            break;
          case 3: // 1.8 KHz
            arm_biquad_cascade_df2T_f32(&S1_CW_AudioFilter4, float_buffer_L, float_buffer_L_AudioCW, 256);//AFP 10-18-22
            arm_copy_f32(float_buffer_L_AudioCW, float_buffer_L, FFT_length / 2);                         //AFP 10-18-22
            arm_copy_f32(float_buffer_L_AudioCW, float_buffer_R, FFT_length / 2);
            break;
          case 4:  // 2.0 KHz
            arm_biquad_cascade_df2T_f32(&S1_CW_AudioFilter5, float_buffer_L, float_buffer_L_AudioCW, 256);//AFP 10-18-22
            arm_copy_f32(float_buffer_L_AudioCW, float_buffer_L, FFT_length / 2);                         //AFP 10-18-22
            arm_copy_f32(float_buffer_L_AudioCW, float_buffer_R, FFT_length / 2);
            break;
          case 5:  //Off
            break;
        }
      }


    }

    // ======================================Interpolation  ================

    arm_fir_interpolate_f32(&FIR_int1_I, float_buffer_L, iFFT_buffer, BUFFER_SIZE * N_BLOCKS / (uint32_t)(DF));   // Interpolatikon
    arm_fir_interpolate_f32(&FIR_int1_Q, float_buffer_R, FFT_buffer, BUFFER_SIZE * N_BLOCKS / (uint32_t)(DF));

    // interpolation-by-4
    arm_fir_interpolate_f32(&FIR_int2_I, iFFT_buffer, float_buffer_L, BUFFER_SIZE * N_BLOCKS / (uint32_t)(DF1));
    arm_fir_interpolate_f32(&FIR_int2_Q, FFT_buffer, float_buffer_R, BUFFER_SIZE * N_BLOCKS / (uint32_t)(DF1));

    /**********************************************************************************  AFP 12-31-20
      Digital Volume Control
    **********************************************************************************/

    if (mute == 1) {
      arm_scale_f32(float_buffer_L, 0.0, float_buffer_L, BUFFER_SIZE * N_BLOCKS);
      arm_scale_f32(float_buffer_R, 0.0, float_buffer_R, BUFFER_SIZE * N_BLOCKS);
    } else if (mute == 0) {
      arm_scale_f32(float_buffer_L, DF * volumeLog[EEPROMData.audioVolume] * 4, float_buffer_L, BUFFER_SIZE * N_BLOCKS);  // Scaled up by factor of 4
      arm_scale_f32(float_buffer_R, DF * volumeLog[EEPROMData.audioVolume] * 4, float_buffer_R, BUFFER_SIZE * N_BLOCKS);  // to achieve comfortable volume.
    }
    /**********************************************************************************  AFP 12-31-20
      CONVERT TO INTEGER AND PLAY AUDIO
    **********************************************************************************/

    for (unsigned  i = 0; i < N_BLOCKS; i++) {
//      sp_L1 = Q_out_L.getBuffer();
//      sp_R1 = Q_out_R.getBuffer();
      arm_float_to_q15 (&float_buffer_L[BUFFER_SIZE * i], Q_out_L.getBuffer(), BUFFER_SIZE);
      arm_float_to_q15 (&float_buffer_R[BUFFER_SIZE * i], Q_out_R.getBuffer(), BUFFER_SIZE);
      Q_out_L.playBuffer(); // play it !
      Q_out_R.playBuffer(); // play it !
    }

//      Codec_gain();

    elapsed_micros_sum = elapsed_micros_sum + usec;
    elapsed_micros_idx_t++;
  } // end of if(audio blocks available)
  if (ms_500.check() == 1)                                  // For clock updates AFP 10-26-22
  {
    //wait_flag = 0;
    DisplayClock();
  }
    
}
