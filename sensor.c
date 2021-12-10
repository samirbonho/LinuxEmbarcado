#include<stdio.h>
#include<fcntl.h>
#include<sys/ioctl.h>
#include<linux/i2c.h>
#include<linux/i2c-dev.h>
#include<unistd.h>

#define BUFFER_SIZE 2

#define DEVICE     0x23 //Default device I2C address
#define POWER_DOWN 0x00 // No active state
#define POWER_ON   0x01 // Power on
#define RESET      0x07 // Reset data register value
#define ONE_TIME_HIGH_RES_MODE 0x20

int main(){
   int file;
   float lumens = 0;
   
   printf("Starting the sensor test application\n");
   
   if((file=open("/dev/i2c-1", O_RDWR)) < 0){
      perror("failed to open the bus\n");
      return 1;
   }
   if(ioctl(file, I2C_SLAVE, DEVICE) < 0){
      perror("Failed to connect to the sensor\n");
      return 1;
   }
   char writeBuffer[1] = {ONE_TIME_HIGH_RES_MODE};
   if(write(file, writeBuffer, 1)!=1){
      perror("Failed to write\n");
      return 1;
   }
   usleep(100); //sleeping for a  while
   char buf[BUFFER_SIZE];
   if(read(file, buf, BUFFER_SIZE)!=BUFFER_SIZE){
      perror("Failed to read in the buffer\n");
      return 1;
   }
   printf("The data read is %x  and %x\n", buf[0], buf[1]);

   lumens = ((buf[1] + (256 * buf[0])) / 1.2);
   printf("Sensor data read is  %f \n", lumens);

   close(file);
   return 0;
}
