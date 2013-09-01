# PollingTrainControlPanel

A Märklin Train Control Panel app which execute on ARM920t devices. Since this is an embedded application, which executes without OS layer, there is no interrupt enabled or handling. In order to offer non-blocking user interaction, the main logic of this app is a polling loop. 

## Overview

This program is designed to control and monitor the Märklin Digital Train Set.   
Feature includes:

* Turn ON/OFF the train set. 
* Assign speed to each train in the range of [0 - 14]
* Reverse a train's heading direction and reaccelerate 
* Assign direction to each switch
* Show the switch status if has been assigned
* Show recent triggered track sensor

## How to...

1. Turn ON the train track
2. Load the program from `ARM/y386wang/train_control_panel.elf`
3. Send `go` to start execution
4. Send one of the following command
	1. `tr <train_id> <speed>` 	assign train speed
	2. `rv <train_id>` reverse train direction and reaccelerate
	3. `sw <switch_id> <direction>` assign switch direction
	4. `q` quit the program
	5. `g` attempt to turn ON the train track
	6. `s` attempt to turn OFF the train track
	
Note: 

* `train_id` and `speed` must be within INTEGER range
* `speed` other than [0 - 14] has unspecified side-effects
* `switch_id` is limited in range [1 - 18] and [153 - 156]
* `direction` is limited as either `C` or `S`

## Program Structure

### 1. Initialization

The initialization of program take the following steps:

1. Polling Loop I/O
	* Create I/O buffer on stack
	* Disable FIFO for both UART
	* Config COM1 to communicate with the train 
2. Elapsed Time Tracking
	* Set the 32-bit timer to free run at 2kHz
	* Get initial timer value
3. Train Commands Queue
	* Construct the Commands Queue	
4. User Input Buffer
	* Construct the buffer
5. Sensor Data Collection
	* Clear COM1's UART receiver buffer
	* Enable sensor value auto reset
6. User Interface
	* Print basic UI components

### 2. Polling Loop

The polling loop is the main loop that running during the entire program life-cycle. Each cycle it does: 

1. For any non-empty buffer, send one-byte of data if the condition below is true
	* Transmit buffer is NOT full
	* COM1 extra: Clear to Send (the receiver on the other end is Clear to Receive)
2. Obtain timer value and increment the elapsed time if necessary
	* If the timer value has been increment for more than 20 since the reference value (1/100 second has passed), increment the elapsed time accordingly. Then save the timer value as the new reference value. 
	* Update elapsed time display
3. Dequeue Train Command if possible
	* Next train command will be send to COM1's IO buffer, if
		1. Train command pausing time is <= zero, and
		2. Current command's delay time is <= zero
	* Otherwise, either decrease pausing time or delay time
4. Collect Sensor data from COM1
	* Parse sensor data if received any, then update the display
	* Send new request if all expected data has been received, or timed out
5. Handle User Input
	* Change command display according to the input
	* If reach EOL, parse the command and send corresponding Train Command
	* If received quit command, tell the loop to break

### 3. Data Structures

1. PL I/O Buffers
	* Each channel has a fixed size char array that store character that going to be sent
	* Each buffer has a send index counter and a save index counter for buffer management	* These form a Circular Buffer that can save as many chars as the array size at the same time
	* One char a time will be tried to send out during the polling loop cycle
2. Train Commands Buffer
	* Each train command is made up with: 
		1. Command byte
		2. delay before send the command (in 1/100s)
		3. length of pause after command has been sent (in 1/100s)
	* Commands are buffered in a Circular Buffer (Similar with the PL I/O Buffer), and will be sent to PL I/O's COM1 Buffer. 
3. Sensor Data from Last-time
	* Data are saved in an byte array, with size of the number of decoder times two. 

As you can tell, circular buffer has been widely used in this project. It is the best choice for now, due to the following advantages: 

1. Access and update to the buffer slot is constant time. 
2. Easy to implement, easy to manage. (Save and Send index loop through the buffer)
3. Data enter and leave the buffer in FIFO fashion

### 4. Known Bugs

1. Try to turn ON the train set while there the train set is OFF and COM1's transmit buffer is full. 
	* The `GO` command will stay in the PL I/O buffer until the train set is ON and clear the transmit buffer
2. Unable to handle Pasting/Sending Script to the program. 
3. Reverse command `rv`'s behavior is dependent to the train model. i.e,
	* Train '35' will decelerate to a slower speed before reverse and reaccelerate
	* Train '48' will perform an emergency stop, then reverse and reaccelerate after 1 second
