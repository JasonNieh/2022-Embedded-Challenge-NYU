#include <mbed.h>
SPI spi(PF_9,PF_8,PF_7); //mosi, miso, sclk
DigitalOut cs(PC_1);

#define READY_FLAG 0b1
#define DONE_FLAG 0b10
#define MINCOUNT_VAL  419430
#define MAXCOUNT_VAL 3774873
//in ms
#define SAMPLE_INTERVAL 200
EventFlags flags;
uint8_t write_buf[4] = {0xAA, 0x00, 0x00, 0x00}; //4 bytes
uint8_t read_buf[4]; // 4 bytes
//derived from read_buf, takes 24 bits
uint32_t rawCounting;
//first 8 bits of status code
uint8_t status;
volatile int heartBeatCount;
//set to true when hit 170
volatile bool readyToDeflate = 0;
//when get back down to a certain level
volatile bool isDeflating = 0;
//update this pressure every new reading comes in
volatile float pressure;
//store last pressure to this for comparison
volatile float lastRead;
//Ticker tic;
float pressureReadings[300];
float oscillation[300];
//dic[0] stores the pressure level at peak
//dic[1] stores the amplitude
volatile float dic[2][500];
volatile int dicIndex = 0;
//oscillation values, peak and valley
volatile float localMin=0;
volatile float localMax=0;
//maximum difference of amplitude
volatile float maxAmp=0;
//to record corresponding pressure level
//volatile int minIndex = 0;
volatile int maxIndex = 0;
//final results
volatile float mean = 0;
volatile float systolic = 0;
volatile float diastolic = 0;
//approximate systolic value = 59% of Max
volatile float sys_estimate = 0;
volatile float temp_difference = 100;

void cb(int event){
  flags.set(DONE_FLAG);
  cs = 1;
}
/*
void fire(){
  flags.set(READY_FLAG);
}
*/
float calculatePressure(uint32_t reading){
  if(reading<MINCOUNT_VAL){
    return 0.0;
  }
  //using the formula provided on the datasheet to convert the count reading
  //to 0-300 mmHg
  float a = (reading - MINCOUNT_VAL)*(300.0)/(MAXCOUNT_VAL-MINCOUNT_VAL);
  return a;
}

//check status bits, status should be 64
void checkStatus(uint8_t status){
  if(status==0b01000000){
    //printf("Reading...\n");
  }
  else if(status & 0b10011010){
    printf("Sensor is malfunctioning...\n");
    thread_sleep_for(100'000'000);
  }
  else if(!(status | 0b01000000)){
    printf("Sensor is not powered...\n");
    thread_sleep_for(100'000'000);
  }
  else if(status & 0b00100000){
    printf("Sensor is busy...\n");
    thread_sleep_for(100'000'000);
  }
  else if (status|0b00000100){
    printf("Integrity check failed...\n");
    thread_sleep_for(100'000'000);
  }
  else if(status|0b00000001){
    printf("Math saturation happened...\n");
    thread_sleep_for(100'000'000);
  }
}
void calculateDeflateRate(){
  // deflate rate should be 4 mmHg per second
  // if deflating quicker or slower than 4 mmHg per second
  // show warning 
  float dRate = (lastRead-pressure) * 1000 / SAMPLE_INTERVAL;
  if(readyToDeflate){
    printf("Deflating at %.2f mmHg/sec now.  ",dRate);
    if(dRate<-10){
      printf("Ready to deflate, please release now.\n");
    }
    else if (dRate>9){
      printf("Deflating rate too high, please slow down.\n");
    }
    else if(dRate>0 && dRate<2){
      printf("Deflating rate too low, please speed up.\n");
    }
    else{
      printf("Please keep this deflating rate.\n");
    }
  }
}

void post_processing(float p[], float oscillation[],int temp){
  printf("Post processing\n");
  for(int i=3; i<temp-3; i++){
    oscillation[i] = p[i] - (p[i-3]+p[i-2]+p[i-1]+p[i+1]+p[i+2]+p[i+3])/6.0;
    printf("Oscillation%d: %f\n", i-3,oscillation[i]);
  }
  volatile int i = 3;
  //get peaks, max amp and get mean BP
  while(p[i]>50){
    while(oscillation[i]<=0){
      if(localMin > oscillation[i]){
        localMin = oscillation[i];
        //minIndex = i;
      }
      i++;
    }
    while(oscillation[i]>0){
      if(localMax < oscillation[i]){
        localMax = oscillation[i];
        maxIndex = i;
      }
      i++;
    }
    heartBeatCount++;
    dic[0][dicIndex] = p[maxIndex];
    dic[1][dicIndex++] = localMax-localMin;
    localMax = 0;
    localMin = 0;
    //minIndex = 0;
    maxIndex = 0;
  }
  //get max amplitude: should be around 100
  for(int i = 0; i<dicIndex; i++){
    if(dic[0][i]<110 && dic[0][i]>85){
      if(dic[1][i]>maxAmp){
        maxAmp = dic[1][i];
        mean = dic[0][i];
      }
    }
    else if(dic[0][i]<=85){
      break;
    }
  }
  sys_estimate = 0.59 * maxAmp;
  //find systolic pressure
  for(int i = 0; i<dicIndex; i++){
    if(dic[0][i]<mean+35 && dic[0][i]>mean){
      //should be absolute
      if(abs(dic[1][i]-sys_estimate)<temp_difference){
        temp_difference = dic[1][i]-sys_estimate;
        systolic = dic[0][i];
      }
    }
    else if(dic[0][i]<=mean){
      break;
    }
  }
  //time period elapsed while monitoring in ms
  //1min 60s 60000 ms
  volatile int timeInterval = temp * SAMPLE_INTERVAL;
  diastolic = (mean - 0.45 * systolic)/0.55;
  printf("\n************ Results ready ************\n\n");
  printf("\tSystolic:  %.2fmmHg\n", systolic);
  printf("\tDiastolic: %.2fmmHg\n",diastolic);
  printf("\tMean:      %.2fmmHg\n", mean);
  printf("\tHeartRate:   %d  BPM\n\n",(int)heartBeatCount*60000/timeInterval);
  printf("  Thank you for taking care of yourself\n");
  printf("\t\tBye!!\n");
  printf("*****************************************\n");
  thread_sleep_for(1'000'000);
}


int main() {
  volatile int temp_counter = 0;
  printf("\n\n\n\nHi! Welcome to 2022 Embedded Challenge. This is Xiaoyu's project.\n");
  printf("Ready to take your blood pressure? \nJust start inflating and gently deflate after hitting 170mmHg as prompted.:)\n");
  cs = 1;
  spi.format(8,0);
  spi.frequency(1'000'000);
  //tic.attach(fire, 1000ms);
  while(1) {
    //flags.wait_all(READY_FLAG);
    cs = 0;
    spi.transfer(write_buf,4,read_buf,4,cb,SPI_EVENT_COMPLETE);
    //de-select chip select, yield SPI after receiving
    flags.wait_all(DONE_FLAG);
    //status is the first byte
    status = read_buf[0];
    //go on if status normal
    //checkStatus(status);
    rawCounting = (read_buf[1]<<16)|(read_buf[2]<<8)|(read_buf[3]);
    pressure = calculatePressure(rawCounting);
    if(lastRead!=0 && pressure>2){
      printf("Current reading: %.2f mmHg\t",pressure); 
      if(!readyToDeflate || !isDeflating){
        printf("\n");
      } 
    }
    if(!readyToDeflate){
      //change this value
      if(pressure>=175){
        readyToDeflate = true;
        printf("Ready to Deflate.\n");
      }
    }
    if(readyToDeflate){
      //change this
      if(pressure<170){
        isDeflating = true;
      }
    }
  /*
    if(readyToDeflate && !isDeflating){
      pressureReadings[temp_counter] = pressure;
      temp_counter++;
      calculateDeflateRate();
      printf("Counter: %d",(int)temp_counter);
      if(temp_counter>=5){
        if(pressureReadings[temp_counter-1]-pressureReadings[0]>4){
          isDeflating = true;
          temp_counter = 0;
        }
      }
    }
  */ 
    
    if(readyToDeflate && isDeflating){
      calculateDeflateRate();
      if(temp_counter<295){
        pressureReadings[temp_counter++] = pressure;
      }
      else{
        printf("Buffer Overflowed. Please try again\n");
        thread_sleep_for(15000);
      }
      if(pressure<40){
        post_processing(pressureReadings,oscillation,temp_counter);
      }
    }
    lastRead = pressure;
    /*
 */
    thread_sleep_for(SAMPLE_INTERVAL);
  }
}