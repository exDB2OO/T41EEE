
#include "SDT.h"

int micGainChoice;


void updateMic() {

  //  This is using the compression after the kneeDB[0] only.
  //  struct compressionCurve crv = { -6.0f, EEPROMData.currentMicGain,           // margin, offset
  //     {0.0f, -20.0f, -1000.0f, -1000.0f, -1000.0f},           // kneeDB[]
  //     {  EEPROMData.currentMicCompRatio, 1.0f, 1.0f, 1.0, 1.0} };   // compressionRatio

  micGain.setGain_dB(EEPROMData.currentMicGain);

  struct compressionCurve crv = { -12.0f, 0.0,  // margin, offset
                                                //   {0.0f, -7.0f, -10.0f, -1000.0f, -1000.0f},           // kneeDB[]
                                  { 0.0f, -7.0f, EEPROMData.currentMicThreshold, -1000.0f, -1000.0f },
                                  //     {  100.0, 100.0f, 1.0f, 1.0, 1.0} };   // compressionRatio
                                  { 100.0, EEPROMData.currentMicCompRatio, 1.0f, 1.0, 1.0 } };


  int16_t delaySize = 256;                    // Any power of 2, i.e., 256, 128, 64, etc.
  compressor1.setDelayBufferSize(delaySize);  // Improves transient response of compressor.

  compressor1.setCompressionCurve(&crv);
  compressor1.begin();
}

/*****
  Purpose: Create I and Q signals from Mic input

  Parameter list:

  Return value;
    void
    Notes:
    There are several actions in this function
    1.  Read in the data from the ADC into the Left Channel at 192KHz
    2.  Format the L data and Decimate (downsample and filter)the sampled data by x8
          - the new effective sampling rate is now 24KHz
    3.  Process the L data through the 7 EQ filters and combine to a single data stream
    4.  Copy the L channel to the R channel
    5.  Process the R and L through two Hilbert Transformers - L 0deg phase shift and R 90 deg ph shift
          - This create the I (L) and Q(R) channels
    6.  Interpolate 8x (upsample and filter) the data stream to 192KHz sample rate
    7.  Output the data stream thruogh the DACs at 192KHz
*****/
//int16_t* sp_L2, sp_R2;
void ExciterIQData() {
  uint32_t N_BLOCKS_EX = N_B_EX;
  float32_t powerScale;

  /**********************************************************************************  AFP 12-31-20
        Get samples from queue buffers
        Teensy Audio Library stores ADC data in two buffers size=128, Q_in_L and Q_in_R as initiated from the audio lib.
        Then the buffers are  read into two arrays sp_L and sp_R in blocks of 128 up to N_BLOCKS.  The arrarys are
        of size BUFFER_SIZE*N_BLOCKS.  BUFFER_SIZE is 128.
        N_BLOCKS = FFT_L / 2 / BUFFER_SIZE * (uint32_t)DF; // should be 16 with DF == 8 and FFT_L = 512
        BUFFER_SIZE*N_BLOCKS = 2024 samples
     **********************************************************************************/
  // are there at least N_BLOCKS buffers in each channel available ?
  if ((uint32_t)Q_in_L_Ex.available() > N_BLOCKS_EX) {

    // get audio samples from the audio  buffers and convert them to float
    // read in 32 blocks á 128 samples in I and Q
    for (unsigned i = 0; i < N_BLOCKS_EX; i++) {
      //      sp_L2 = Q_in_L_Ex.readBuffer();
      //      sp_R2 = Q_in_R_Ex.readBuffer();

      /**********************************************************************************  AFP 12-31-20
          Using arm_Math library, convert to float one buffer_size.
          Float_buffer samples are now standardized from > -1.0 to < 1.0
      **********************************************************************************/
      arm_q15_to_float(Q_in_L_Ex.readBuffer(), &float_buffer_L_EX[BUFFER_SIZE * i], BUFFER_SIZE);  // convert int_buffer to float 32bit
      arm_q15_to_float(Q_in_R_Ex.readBuffer(), &float_buffer_R_EX[BUFFER_SIZE * i], BUFFER_SIZE);  // Right channel not used.  KF5N March 11, 2024
      Q_in_L_Ex.freeBuffer();
      Q_in_R_Ex.freeBuffer();  // Right channel not used.  KF5N March 11, 2024
    }

    //    float exciteMaxL = 0;

    /**********************************************************************************  AFP 12-31-20
              Decimation is the process of downsampling the data stream and LP filtering
              Decimation is done in two stages to prevent reversal of the spectrum, which occurs with each even
              Decimation.  First select every 4th sample and then every 2nd sample, yielding 8x downsampling
              192KHz/8 = 24KHz, with 8xsmaller sample sizes
     **********************************************************************************/

    // 192KHz effective sample rate here
    // decimation-by-4 in-place!
    //    arm_fir_decimate_f32(&FIR_dec1_EX_I, float_buffer_L_EX, float_buffer_L_EX, BUFFER_SIZE * N_BLOCKS_EX );
    //    arm_fir_decimate_f32(&FIR_dec1_EX_Q, float_buffer_R_EX, float_buffer_R_EX, BUFFER_SIZE * N_BLOCKS_EX ); // Right channel not used.  KF5N March 11, 2024
    // 48KHz effective sample rate here
    // decimation-by-2 in-place
    //arm_fir_decimate_f32(&FIR_dec2_EX_I, float_buffer_L_EX, float_buffer_L_EX, 512);
    //    arm_fir_decimate_f32(&FIR_dec2_EX_Q, float_buffer_R_EX, float_buffer_R_EX, 512); // Right channel not used.  KF5N March 11, 2024

    //============================  Transmit EQ  ========================  AFP 10-02-22
    //    if (EEPROMData.xmitEQFlag == ON ) {
    //      DoExciterEQ();  // The exciter equalizer works with left channel data only.
    //    }

    // Microphone audio has only 1 channel, so copy left to right.
    //    arm_copy_f32 (float_buffer_L_EX, float_buffer_R_EX, 256);

    // =========================    End CW Xmit
    //--------------  Hilbert Transformers

    /**********************************************************************************
             R and L channels are processed though the two Hilbert Transformers, L at 0 deg and R at 90 deg
             Tthe result are the quadrature data streans, I and Q necessary for Phasing calculations to
             create the SSB signals.
             Two Hilbert Transformers are used to preserve eliminate the relative time delays created during processing of the data
    **********************************************************************************/
    //    arm_fir_f32(&FIR_Hilbert_L, float_buffer_L_EX, float_buffer_L_EX, 256);
    //    arm_fir_f32(&FIR_Hilbert_R, float_buffer_R_EX, float_buffer_R_EX, 256);

    /**********************************************************************************
              Additional scaling, if nesessary to compensate for down-stream gain variations
     **********************************************************************************/

    if (bands[EEPROMData.currentBand].mode == DEMOD_LSB) {  //AFP 12-27-21
      //arm_scale_f32 (float_buffer_L_EX, -EEPROMData.IQXAmpCorrectionFactor[EEPROMData.currentBandA], float_buffer_L_EX, 256);
      arm_scale_f32(float_buffer_L_EX, +EEPROMData.IQSSBAmpCorrectionFactor[EEPROMData.currentBandA], float_buffer_L_EX, 2048);  // Flip SSB sideband KF5N, minus sign was original
      IQPhaseCorrection(float_buffer_L_EX, float_buffer_R_EX, EEPROMData.IQSSBPhaseCorrectionFactor[EEPROMData.currentBandA], 2048);
    } else if (bands[EEPROMData.currentBand].mode == DEMOD_USB) {  //AFP 12-27-21
      //arm_scale_f32 (float_buffer_L_EX, + EEPROMData.IQXAmpCorrectionFactor[EEPROMData.currentBandA], float_buffer_L_EX, 256);     // Flip SSB sideband KF5N, minus sign was original
      arm_scale_f32(float_buffer_L_EX, -EEPROMData.IQSSBAmpCorrectionFactor[EEPROMData.currentBandA], float_buffer_L_EX, 2048);  // Flip SSB sideband KF5N
      IQPhaseCorrection(float_buffer_L_EX, float_buffer_R_EX, EEPROMData.IQSSBPhaseCorrectionFactor[EEPROMData.currentBandA], 2048);
    }
    //    arm_scale_f32 (float_buffer_R_EX, 1.00, float_buffer_R_EX, 256);

    //    exciteMaxL = 0;
    //    for (int k = 0; k < 256; k++) {
    //      if (float_buffer_L_EX[k] > exciteMaxL) {
    //        exciteMaxL = float_buffer_L_EX[k];
    //      }
    //    }

    /**********************************************************************************
              Interpolate (upsample the data streams by 8X to create the 192KHx sample rate for output
              Requires a LPF FIR 48 tap 10KHz and 8KHz
     **********************************************************************************/
    //24KHz effective sample rate here
    //    arm_fir_interpolate_f32(&FIR_int1_EX_I, float_buffer_L_EX, float_buffer_LTemp, 256);
    //arm_fir_interpolate_f32(&FIR_int1_EX_Q, float_buffer_R_EX, float_buffer_RTemp, 256);

    // interpolation-by-4,  48KHz effective sample rate here
    //arm_fir_interpolate_f32(&FIR_int2_EX_I, float_buffer_LTemp, float_buffer_L_EX, 512);
    //    arm_fir_interpolate_f32(&FIR_int2_EX_Q, float_buffer_RTemp, float_buffer_R_EX, 512);
    //  192KHz effective sample rate here

    //  This is the correct place in the data stream to inject the scaling for power.
#ifdef QSE2
    powerScale = 10.0 * EEPROMData.powerOutSSB[EEPROMData.currentBand];
#else
    powerScale = 7.0 * EEPROMData.powerOutSSB[EEPROMData.currentBand];
#endif

    arm_scale_f32(float_buffer_L_EX, powerScale, float_buffer_L_EX, 2048);  //Scale to compensate for losses in Interpolation
    arm_scale_f32(float_buffer_R_EX, powerScale, float_buffer_R_EX, 2048);

    /**********************************************************************************  AFP 12-31-20
      CONVERT TO INTEGER AND PLAY AUDIO
    **********************************************************************************/
    q15_t q15_buffer_LTemp[2048];  // KF5N
    q15_t q15_buffer_RTemp[2048];  // KF5N

    arm_float_to_q15(float_buffer_L_EX, q15_buffer_LTemp, 2048);
    arm_float_to_q15(float_buffer_R_EX, q15_buffer_RTemp, 2048);
#ifdef QSE2
    arm_offset_q15(q15_buffer_LTemp, EEPROMData.iDCoffsetSSB[EEPROMData.currentBand] + EEPROMData.dacOffsetSSB, q15_buffer_LTemp, 2048);  // Carrier suppression offset.
    arm_offset_q15(q15_buffer_RTemp, EEPROMData.qDCoffsetSSB[EEPROMData.currentBand] + EEPROMData.dacOffsetSSB, q15_buffer_RTemp, 2048);
#endif
    Q_out_L_Ex.play(q15_buffer_LTemp, 2048);  // play it!  This is the I channel from the Audio Adapter line out to QSE I input.
    Q_out_R_Ex.play(q15_buffer_RTemp, 2048);  // play it!  This is the Q channel from the Audio Adapter line out to QSE Q input.

    /*
    for (unsigned  i = 0; i < N_BLOCKS_EX; i++) {  //N_BLOCKS_EX=16  BUFFER_SIZE=128 16x128=2048
//      sp_L2 = Q_out_L_Ex.getBuffer();
//      sp_R2 = Q_out_R_Ex.getBuffer();
      arm_float_to_q15 (&float_buffer_L_EX[BUFFER_SIZE * i], Q_out_L_Ex.getBuffer(), BUFFER_SIZE);
      arm_float_to_q15 (&float_buffer_R_EX[BUFFER_SIZE * i], Q_out_R_Ex.getBuffer(), BUFFER_SIZE);
      arm_offset_q15(Q_out_L_Ex.getBuffer(), EEPROMData.iDCoffset[EEPROMData.currentBand] + 1900, Q_out_L_Ex.getBuffer(), 128);  // Carrier suppression offset.
      arm_offset_q15(Q_out_R_Ex.getBuffer(), EEPROMData.qDCoffset[EEPROMData.currentBand] + 1900, Q_out_R_Ex.getBuffer(), 128);
      Q_out_L_Ex.playBuffer(); // play it !
      Q_out_R_Ex.playBuffer(); // play it !
    }
    */
  }
}

/*****
  Purpose: Set the current band relay ON or OFF

  Parameter list:
    int state             OFF = 0, ON = 1

  Return value;
    void
*****/
void SetBandRelay(int state) {
  // There are 4 physical relays.  Turn all of them off.
  for (int i = 0; i < 4; i = i + 1) {
    digitalWrite(bandswitchPins[i], LOW);  // Set ALL band relays low.  KF5N July 21, 2023
  }
  // Set current band relay "on".  Ignore 12M and 10M.  15M and 17M use the same relay.  KF5N September 27, 2023.
  if (EEPROMData.currentBand < 5) digitalWrite(bandswitchPins[EEPROMData.currentBand], state);
}


/*****
  Purpose: Allow user to set the microphone compression level.
           A typical value is -20 dB.
  Parameter list:
    void

  Return value;
    void
*****/
void SetCompressionThreshold() {
  int val;

  tft.setFontScale((enum RA8875tsize)1);
  tft.fillRect(SECONDARY_MENU_X - 50, MENUS_Y, EACH_MENU_WIDTH + 50, CHAR_HEIGHT, RA8875_MAGENTA);
  tft.setTextColor(RA8875_WHITE);
  tft.setCursor(SECONDARY_MENU_X - 48, MENUS_Y + 1);
  tft.print("Comp Thresh dB:");
  tft.setCursor(SECONDARY_MENU_X + 195, MENUS_Y + 1);
  tft.print(EEPROMData.currentMicThreshold, 0);

  while (true) {
    if (filterEncoderMove != 0) {
      EEPROMData.currentMicThreshold += ((float)filterEncoderMove);
      if (EEPROMData.currentMicThreshold < -60)
        EEPROMData.currentMicThreshold = -60;
      else if (EEPROMData.currentMicThreshold > 0)  // 100% max
        EEPROMData.currentMicThreshold = 0;

      tft.fillRect(SECONDARY_MENU_X + 195, MENUS_Y, 80, CHAR_HEIGHT, RA8875_MAGENTA);
      tft.setCursor(SECONDARY_MENU_X + 195, MENUS_Y + 1);
      tft.print(EEPROMData.currentMicThreshold, 0);
      filterEncoderMove = 0;
    }
    val = ReadSelectedPushButton();  // Read pin that controls all switches
    val = ProcessButtonPress(val);
    delay(150L);
    if (val == MENU_OPTION_SELECT) {  // Make a choice??
      updateMic();
      EEPROMWrite();
      UpdateCompressionField();
      break;
    }
  }
  EraseMenus();
}


/*****
  Purpose: Allow user to set the microphone compression ratio.
           A typical value is in the 10 to 100 range.
  Parameter list:
    void

  Return value;
    void
*****/
void SetCompressionRatio() {
  int val;

  tft.setFontScale((enum RA8875tsize)1);

  tft.fillRect(SECONDARY_MENU_X - 50, MENUS_Y, EACH_MENU_WIDTH + 50, CHAR_HEIGHT, RA8875_MAGENTA);
  tft.setTextColor(RA8875_WHITE);
  tft.setCursor(SECONDARY_MENU_X - 48, MENUS_Y + 1);
  tft.print("Comp Ratio:");
  tft.setCursor(SECONDARY_MENU_X + 180, MENUS_Y + 1);
  tft.print(EEPROMData.currentMicCompRatio, 0);

  while (true) {
    if (filterEncoderMove != 0) {
      EEPROMData.currentMicCompRatio += ((float)filterEncoderMove * 1.0);
      if (EEPROMData.currentMicCompRatio > 1000)
        EEPROMData.currentMicCompRatio = 1000;
      else if (EEPROMData.currentMicCompRatio < 1)  // 100% max
        EEPROMData.currentMicCompRatio = 1;

      tft.fillRect(SECONDARY_MENU_X + 180, MENUS_Y, 80, CHAR_HEIGHT, RA8875_MAGENTA);
      tft.setCursor(SECONDARY_MENU_X + 180, MENUS_Y + 1);
      tft.print(EEPROMData.currentMicCompRatio, 0);
      filterEncoderMove = 0;
    }

    val = ReadSelectedPushButton();  // Read pin that controls all switches
    val = ProcessButtonPress(val);
    delay(150L);

    if (val == MENU_OPTION_SELECT) {  // Make a choice??
      // EEPROMData.EEPROMData.currentMicCompRatio = EEPROMData.currentMicCompRatio;
      updateMic();  // This updates the compression ratio and the threshold.
      EEPROMWrite();
      break;
    }
  }
  EraseMenus();
}


/*****
  Purpose: Set microphone gain.  The default is 0 dB.

  Parameter list:
    void

  Return value
    int           an index into the band array
*****/
void MicGainSet() {
      int val;
      tft.setFontScale((enum RA8875tsize)1);
      tft.fillRect(SECONDARY_MENU_X - 50, MENUS_Y, EACH_MENU_WIDTH + 50, CHAR_HEIGHT, RA8875_MAGENTA);
      tft.setTextColor(RA8875_WHITE);
      tft.setCursor(SECONDARY_MENU_X - 48, MENUS_Y + 1);
      tft.print("Mic Gain dB:");
      tft.setCursor(SECONDARY_MENU_X + 160, MENUS_Y + 1);
      tft.print(EEPROMData.currentMicGain, 1);
      while (true) {
        if (filterEncoderMove != 0) {
          EEPROMData.currentMicGain += ((float)filterEncoderMove);
          if (EEPROMData.currentMicGain < -20)
            EEPROMData.currentMicGain = -20;
          else if (EEPROMData.currentMicGain > 20)  // 100% max
            EEPROMData.currentMicGain = 20;
          tft.fillRect(SECONDARY_MENU_X + 160, MENUS_Y, 80, CHAR_HEIGHT, RA8875_MAGENTA);
          tft.setCursor(SECONDARY_MENU_X + 160, MENUS_Y + 1);
          tft.print(EEPROMData.currentMicGain, 1);
          filterEncoderMove = 0;
        }
        val = ReadSelectedPushButton();
        val = ProcessButtonPress(val);
        if (val == MENU_OPTION_SELECT) {
          updateMic();                    // Update the Open Audio compressor.
          EEPROMWrite();
          break;
        }
      }
  }



/*****
  Purpose: Allow user to set the mic Attack in sec

  Parameter list:
    void

  Return value;
    void
*****
void SetCompressionAttack()
{
  int val;

  tft.setFontScale( (enum RA8875tsize) 1);

  tft.fillRect(SECONDARY_MENU_X - 50, MENUS_Y, EACH_MENU_WIDTH + 50, CHAR_HEIGHT, RA8875_MAGENTA);
  tft.setTextColor(RA8875_WHITE);
  tft.setCursor(SECONDARY_MENU_X  - 48, MENUS_Y + 1);
  tft.print("Attack Sec:");
  tft.setCursor(SECONDARY_MENU_X + 180, MENUS_Y + 1);
  tft.print(EEPROMData.currentMicAttack, 1);

  while (true) {
    if (filterEncoderMove != 0) {
      EEPROMData.currentMicAttack += ((float) filterEncoderMove * 0.1);
      if (EEPROMData.currentMicAttack > 10)
        EEPROMData.currentMicAttack = 10;
      else if (EEPROMData.currentMicAttack < .1)                 // 100% max
        EEPROMData.currentMicAttack = .1;

      tft.fillRect(SECONDARY_MENU_X + 180, MENUS_Y, 80, CHAR_HEIGHT, RA8875_MAGENTA);
      tft.setCursor(SECONDARY_MENU_X + 180, MENUS_Y + 1);
      tft.print(EEPROMData.currentMicAttack, 1);
      filterEncoderMove = 0;
    }

    val = ReadSelectedPushButton();                                  // Read pin that controls all switches
    val = ProcessButtonPress(val);
    delay(150L);

    if (val == MENU_OPTION_SELECT) {                             // Make a choice??
      //EEPROMData.EEPROMData.currentMicAttack = EEPROMData.currentMicAttack;
      EEPROMWrite();

      break;
    }
  }
  EraseMenus();
}
*/

/*****
  Purpose: Allow user to set the mic compression ratio

  Parameter list:
    void

  Return value;
    void
*****
void SetCompressionRelease()
{
  int val;

  tft.setFontScale( (enum RA8875tsize) 1);

  tft.fillRect(SECONDARY_MENU_X - 50, MENUS_Y, EACH_MENU_WIDTH + 50, CHAR_HEIGHT, RA8875_MAGENTA);
  tft.setTextColor(RA8875_WHITE);
  tft.setCursor(SECONDARY_MENU_X  - 48, MENUS_Y + 1);
  tft.print("Decay Sec:");
  tft.setCursor(SECONDARY_MENU_X + 180, MENUS_Y + 1);
  tft.print(EEPROMData.currentMicRelease, 1);

  while (true) {
    if (filterEncoderMove != 0) {
      EEPROMData.currentMicRelease += ((float) filterEncoderMove * 0.1);
      if (EEPROMData.currentMicRelease > 10)
        EEPROMData.currentMicRelease = 10;
      else if (EEPROMData.currentMicRelease < 0.1)                 // 100% max
        EEPROMData.currentMicRelease = 0.1;

      tft.fillRect(SECONDARY_MENU_X + 180, MENUS_Y, 80, CHAR_HEIGHT, RA8875_MAGENTA);
      tft.setCursor(SECONDARY_MENU_X + 180, MENUS_Y + 1);
      tft.print(EEPROMData.currentMicRelease, 1);
      filterEncoderMove = 0;
    }

    val = ReadSelectedPushButton();                                  // Read pin that controls all switches
    val = ProcessButtonPress(val);
    delay(150L);

    if (val == MENU_OPTION_SELECT) {                             // Make a choice??
      //EEPROMData.EEPROMData.currentMicCompRatio = EEPROMData.currentMicCompRatio;
      EEPROMWrite();

      break;
    }
  }
  EraseMenus();
}
*/