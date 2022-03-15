#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio.h>       // Required for the GPIO functions
#include <linux/interrupt.h>  // Required for the IRQ code
#include <linux/kobject.h>    // Using kobjects for the sysfs bindings
#include <linux/time.h>       // Using clock to measure button press times
#define  DEBOUNCE_TIME 300    // The default bounce time -- 300ms

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Samir Bonho");
MODULE_DESCRIPTION("Módulo que utiliza GPIO, Interrupção e kobjects");
MODULE_VERSION("1.0");

static bool isRising = 1;                 // rising edge default IRQ property
module_param(isRising, bool, S_IRUGO);    // S_IRUGO read/not changed
MODULE_PARM_DESC(isRising, " Rising edge = 1 (default), Falling edge = 0");

static unsigned int gpioButton = 27;      // default GPIO is 27
module_param(gpioButton, uint, S_IRUGO);  // S_IRUGO can be read/not changed
MODULE_PARM_DESC(gpioButton, " GPIO Button number (default=27)");

static unsigned int gpioLED = 17;         // default GPIO is 17
module_param(gpioLED, uint, S_IRUGO);     // S_IRUGO can be read/not changed
MODULE_PARM_DESC(gpioLED, " GPIO LED number (default=17)");

static char   gpioName[8] = "gpioXXX";    // null terminated default string
static int    irqNumber;                  // used to share the IRQ number
static int    numberPresses = 0;          // store number of button presses
static bool   ledOn = 0;                  // used to invert the LED state
static bool   isDebounce = 1;             // use to store debounce state
static struct timespec ts_last, ts_current, ts_diff;  // nano precision

// Function prototype for the custom IRQ handler function
static irq_handler_t  erpi_gpio_irq_handler(unsigned int irq, 
                                     void *dev_id, struct pt_regs *regs);


static ssize_t numberPresses_show(struct kobject *kobj, 
                                  struct kobj_attribute *attr, char *buf) {
   return sprintf(buf, "%d\n", numberPresses);
}

static ssize_t numberPresses_store(struct kobject *kobj, struct 
                    kobj_attribute *attr, const char *buf, size_t count) {
   sscanf(buf, "%du", &numberPresses);
   return count;
}


static ssize_t ledOn_show(struct kobject *kobj, struct kobj_attribute *attr, 
                          char *buf) {
   return sprintf(buf, "%d\n", ledOn);
}

static ssize_t lastTime_show(struct kobject *kobj, 
                             struct kobj_attribute *attr, char *buf){
   return sprintf(buf, "%.2lu:%.2lu:%.2lu:%.9lu \n", (ts_last.tv_sec/3600)%24,
          (ts_last.tv_sec/60) % 60, ts_last.tv_sec % 60, ts_last.tv_nsec );
}

static ssize_t diffTime_show(struct kobject *kobj, 
                             struct kobj_attribute *attr, char *buf){
   return sprintf(buf, "%lu.%.9lu\n", ts_diff.tv_sec, ts_diff.tv_nsec);
}

static ssize_t isDebounce_show(struct kobject *kobj, 
                               struct kobj_attribute *attr, char *buf){
   return sprintf(buf, "%d\n", isDebounce);
}

static ssize_t isDebounce_store(struct kobject *kobj, struct kobj_attribute 
                                *attr, const char *buf, size_t count){
   unsigned int temp;
   sscanf(buf, "%du", &temp);       // use temp var for correct int->bool
   gpio_set_debounce(gpioButton,0);
   isDebounce = temp;
   if(isDebounce) { gpio_set_debounce(gpioButton, DEBOUNCE_TIME);
      printk(KERN_INFO "ERPi Button: Debounce on\n");
   }
   else { gpio_set_debounce(gpioButton, 0);  // set the debounce time to 0
      printk(KERN_INFO "ERPi Button: Debounce off\n");
   }
   return count;
}


static struct kobj_attribute count_attr = __ATTR(numberPresses, 0664, numberPresses_show, numberPresses_store);
static struct kobj_attribute debounce_attr = __ATTR(isDebounce, 0664, isDebounce_show, isDebounce_store);


static struct kobj_attribute ledon_attr = __ATTR_RO(ledOn);
static struct kobj_attribute time_attr  = __ATTR_RO(lastTime);
static struct kobj_attribute diff_attr  = __ATTR_RO(diffTime);


static struct attribute *erpi_attrs[] = {
      &count_attr.attr,        // the number of button presses
      &ledon_attr.attr,        // is the LED on or off?
      &time_attr.attr,         // button press time in HH:MM:SS:NNNNNNNNN
      &diff_attr.attr,         // time difference between last two presses
      &debounce_attr.attr,     // is debounce state true or false
      NULL,
};


static struct attribute_group attr_group = {
      .name  = gpioName,       // the name generated in erpi_button_init()
      .attrs = erpi_attrs,     // the attributes array defined just above
};

static struct kobject *erpi_kobj;


static int __init erpi_button_init(void){
   int result = 0;
   unsigned long IRQflags = IRQF_TRIGGER_RISING;
   printk(KERN_INFO "ERPi Button: Initializing the button LKM\n");
   sprintf(gpioName, "gpio%d", gpioButton);   // create /sys/erpi/gpio27

   // create the kobject sysfs entry at /sys/erpi
   erpi_kobj = kobject_create_and_add("erpi", kernel_kobj->parent);
   if(!erpi_kobj){
      printk(KERN_ALERT "ERPi Button: failed to create kobject mapping\n");
      return -ENOMEM;
   }
   // add the attributes to /sys/erpi/ e.g., /sys/erpi/gpio27/numberPresses
   result = sysfs_create_group(erpi_kobj, &attr_group);
   if(result) {
      printk(KERN_ALERT "ERPi Button: failed to create sysfs group\n");
      kobject_put(erpi_kobj);               // clean up remove entry
      return result;
   }
   getnstimeofday(&ts_last);                // set last time to current time
   ts_diff = timespec_sub(ts_last, ts_last);  // set the initial time diff=0

   // set up the LED. It is a GPIO in output mode and will be on by default
   ledOn = true;
   gpio_request(gpioLED, "sysfs");          // gpioLED is hardcoded to 17
   gpio_direction_output(gpioLED, ledOn);   // set in output mode
   gpio_export(gpioLED, false);             // appears in /sys/class/gpio/
   gpio_request(gpioButton, "sysfs");       // set up the gpioButton
   gpio_direction_input(gpioButton);        // set up as an input
   gpio_set_debounce(gpioButton, DEBOUNCE_TIME); // ddebounce the button
   gpio_export(gpioButton, false);          // appears in /sys/class/gpio/
   printk(KERN_INFO "ERPi Button: button state: %d\n", 
          gpio_get_value(gpioButton));
   irqNumber = gpio_to_irq(gpioButton);
   printk(KERN_INFO "ERPi Button: button mapped to IRQ: %d\n", irqNumber);
   if(!isRising){                           // if kernel param isRising=0
      IRQflags = IRQF_TRIGGER_FALLING;      // set on falling edge
   }
   // This next call requests an interrupt line
   result = request_irq(irqNumber,             // the interrupt number
                        (irq_handler_t) erpi_gpio_irq_handler,
                        IRQflags,              // use custom kernel param
                        "erpi_button_handler", // used in /proc/interrupts
                        NULL);                 // the *dev_id for shared lines
   return result;
}

static void __exit erpi_button_exit(void){
   printk(KERN_INFO "ERPi Button: The button was pressed %d times\n", 
          numberPresses);
   kobject_put(erpi_kobj);          // clean up, remove kobject sysfs entry
   gpio_set_value(gpioLED, 0);      // turn the LED off, device was unloaded
   gpio_unexport(gpioLED);          // unexport the LED GPIO
   free_irq(irqNumber, NULL);       // free the IRQ number, no *dev_id required in this case
   gpio_unexport(gpioButton);       // unexport the Button GPIO
   gpio_free(gpioLED);              // free the LED GPIO
   gpio_free(gpioButton);           // free the Button GPIO
   printk(KERN_INFO "ERPi Button: Goodbye from the ERPi Button LKM!\n");
}


static irq_handler_t erpi_gpio_irq_handler(unsigned int irq, 
                                    void *dev_id, struct pt_regs *regs){
   ledOn = !ledOn;                   // invert LED on each button press
   gpio_set_value(gpioLED, ledOn);   // set the physical LED accordingly
   getnstimeofday(&ts_current);      // get the current time as ts_current
   ts_diff = timespec_sub(ts_current, ts_last);   // determine the time diff
   ts_last = ts_current;             // store current time as ts_last
   printk(KERN_INFO "ERPi Button: The button state is currently: %d\n", 
                   gpio_get_value(gpioButton));
   numberPresses++;                  // count number of presses
   return (irq_handler_t) IRQ_HANDLED;  // announce IRQ was handled correctly
}


module_init(erpi_button_init);
module_exit(erpi_button_exit);
