// --------------------------------------------------------------
// Names and IDs: Celine Fong (1580124), Claire Martin (1571140)
// CMPUT 275 Winter 2020
//
// Major Assignment #1
// Part 1: Simple Restaurant Finder
// --------------------------------------------------------------


#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <MCUFRIEND_kbv.h>
#include <SD.h>
#include <TouchScreen.h>
#include <SPI.h>
#include "lcd_image.h"

#define SD_CS 10

// physical dimensions of the tft display (# of pixels)
#define DISPLAY_WIDTH  480
#define DISPLAY_HEIGHT 320

// touch screen pins, obtained from the documentaion
#define YP A3 // must be an analog pin, use "An" notation!
#define XM A2 // must be an analog pin, use "An" notation!
#define YM 9  // can be a digital pin
#define XP 8  // can be a digital pin

// define constraints of cursor on screen
#define CURSOR_X_MAX (DISPLAY_WIDTH - 60) - (CURSOR_SIZE/2) - 1
#define CURSOR_Y_MAX DISPLAY_HEIGHT - CURSOR_SIZE/2 - 1

// dimensions of the part allocated to the map display
#define MAP_DISP_WIDTH (DISPLAY_WIDTH - 60)
#define MAP_DISP_HEIGHT DISPLAY_HEIGHT

#define REST_START_BLOCK 4000000
#define NUM_RESTAURANTS 1066

// calibration data for the touch screen, obtained from documentation
// the minimum/maximum possible readings from the touch point
#define TS_MINX 100
#define TS_MINY 120
#define TS_MAXX 940
#define TS_MAXY 920

// thresholds to determine if there was a touch
#define MINPRESSURE   10
#define MAXPRESSURE 1000

// define joystick info
#define JOYSTICK_VERT	A9 // A9 to VRx
#define JOYSTICK_HORIZ	A8 // A8 to VRy
#define JOYSTICK_SEL	53 // 53 to SW

// define map constants
#define MAP_WIDTH 2048
#define MAP_HEIGHT 2048
#define LAT_NORTH 5361858l
#define LAT_SOUTH 5340953l
#define LON_WEST -11368652l
#define LON_EAST -11333496l

// define map constraints for drawing map patches
#define YEG_X_MAX MAP_WIDTH - MAP_DISP_WIDTH
#define YEG_Y_MAX MAP_HEIGHT - MAP_DISP_HEIGHT

// define middle of map for initial map patch drawn
#define YEG_MIDDLE_X MAP_WIDTH/2 - MAP_DISP_WIDTH/2
#define YEG_MIDDLE_Y MAP_HEIGHT/2 - MAP_DISP_HEIGHT/2

// declare map
lcd_image_t yegImage = {"yeg-big.lcd", MAP_WIDTH, MAP_HEIGHT};

// thresholds for the joystick
#define JOY_CENTER   512
#define JOY_DEADZONE 64
#define BUFFER 400
#define CURSOR_SIZE 9

MCUFRIEND_kbv tft;

// a multimeter reading says there are 300 ohms of resistance across the plate,
// so initialize with this to get more accurate readings
TouchScreen ts = TouchScreen(XP, YP, XM, YM, 300);
Sd2Card card;

// initialize and declare global variables and structs
// global variables used exclusively in mode0

// current cursor position on the display
// should always be within 0 and display constraints, defined above
int cursorX, cursorY;

// previous cursor position on the display
// used to redraw map portions over previous cursor position
int prevX, prevY;

// coordinates of current upper left corner of map
// should always be within 0 and map constraints defined above
int yegCurrX, yegCurrY;

// notes that no mode switching has occurred yet
bool firstTime = true;

// notes whether the restaurant dots are drawn or not
bool isDrawn = false;

// forward declaration for redrawing cursor
void redrawCursor(uint16_t colour);

// restaurant struct, from weekly exercise
struct Restaurant {
	int32_t lat; // Stored in 1/100,000 degrees
	int32_t lon; // Stored in 1/100,000 degrees
	uint8_t rating; // from 0 to 10
	char name[55]; // alread null terminated in SD card
};

// restDist struct, stores index and distance from current 
// cursor location
// can use index to pull info from corresponding Restaurant struct
struct RestDist {
	uint16_t index; // index of restaurant from 0 to NUM_RESTAURANTS-1
	uint16_t dist; // Manhattan distance to cursor position
};

// global variables used in mode1
// array containing 1066 RestDist structures
RestDist rest_dist[NUM_RESTAURANTS];

// Initialize global variables oldBlock and restBlock used in the fast method
uint32_t oldBlock = 0;
Restaurant restBlock[8];

// defines the restaurant that is currently selected
int selectedRest = 0;

// These functions convert between x/y map position and lat/lon
int32_t x_to_lon(int16_t x) {
	return map(x, 0, MAP_WIDTH, LON_WEST, LON_EAST);
}

int32_t y_to_lat(int16_t y) {
	return map(y, 0, MAP_HEIGHT, LAT_NORTH, LAT_SOUTH);
}

int16_t lon_to_x(int32_t lon) {
	return map(lon, LON_WEST, LON_EAST, 0, MAP_WIDTH);
}

int16_t lat_to_y(int32_t lat) {
	return map(lat, LAT_NORTH, LAT_SOUTH, 0, MAP_HEIGHT);
}

/* 
	Retrieves a new restaurant block only if the current restaurant is not
	in the current block.

	Arguments:
		restIndex (int): The index of the restaurant (0 to 1065)
		restPtr (Restaurant*): Points to the restaurant address

	Returns:
		N/A
*/
void getRestaurant(int restIndex, Restaurant* restPtr) {
	// determine block number from restIndex
	uint32_t blockNum = REST_START_BLOCK + restIndex/8;

	// if the restaurant is in a new block, read the new block
	if (blockNum != oldBlock) {
		while (!card.readBlock(blockNum, (uint8_t*) restBlock)) {
		    Serial.println("Read block failed, trying again.");
		}
	}

	// if the restaurant is in the same block, just get it from the block
	*restPtr = restBlock[restIndex % 8];

	// reset oldBlock to be equal to the current block
	oldBlock = blockNum;
}

/* 
	Calculates Manhattan distance between two points
	
	Arguments:
		x1 (int16_t): x coordinate of first point
		x2 (int16_t): x coordinate of second point
		y1 (int16_t): y coordinate of first point
		y2 (int16_t): y coordinate of second point

	Returns:
		dist (int16_t): Manhattan distance between points
*/
int16_t manhattanDist(int16_t x1,int16_t x2,int16_t y1,int16_t y2) {
	int16_t dist = abs(x1 - x2) + abs(y1 - y2);
	return dist;
}

/*
	Gets an array of RestDist structures based on current location

	Arguments:
		restDistArray (RestDist*): array of RestDist structs
		x (int16_t): current x location of cursor in terms of YEG map 
		y (int16_t): current y location of cursor in terms of YEG map

	Returns:
		N/A
*/
void getRestDist(RestDist* restDistArray, int16_t x, int16_t y) {
	// load array of RestDist structures
	Restaurant rest;
	for (int i = 0; i < NUM_RESTAURANTS; i++) {
		// load a restaurant from SD card
		getRestaurant(i, &rest);
		restDistArray[i].index = i;
		// store index and distance as RestDist struct in rest_dist array
		int32_t lat = rest.lat;
		int32_t lon = rest.lon;
		int16_t xPoint = lon_to_x(lon);
		int16_t yPoint = lat_to_y(lat);
		// calculate manhattan distance and store in rest_dist
		restDistArray[i].dist = manhattanDist(xPoint, x, yPoint, y);
	}
}

/*
	Swaps two RestDist structures (used in insertion sort)

	Arguments:
		x (RestDist&): pass-by-reference to x coordinate of restaurant
		y (RestDist&): pass-by-reference to y coordinate of restaurant

	Returns:
		N/A
*/
void swap(RestDist &x,RestDist &y) {
	RestDist temp = x; // stores x in a temporary variable
	x = y; // puts y into x's previous memory address
	y = temp; // puts x into y's previous memory address
}

/*
	Insertion sort function, taken from assignment description, sorts by distance from cursor

	Arguments:
		distArray (RestDist*): declares a pointer to array storing index and distances of restaurants
		length (int): number of restaurants stored in array

	Returns: 
		N/A
*/
void isort(RestDist* distArray, int length) {
	for (int i = 1; i < length; ++i) {
		for (int j = i; j > 0 && distArray[j].dist < distArray[j-1].dist; --j) {
			swap(distArray[j], distArray[j-1]);
		}
	}
}

/* 
	Displays the list of closest restaurants to the cursor coordinates
	
	Arguments: 
		restArray (RestDist*): Array of restDist structs

	Returns:
		N/A
*/
void displayNames(RestDist* restArray) {
	tft.fillScreen(0);
	tft.setCursor(0,0);

	for (int16_t i = 0; i < 21; i++) {
		Restaurant r;
		getRestaurant(restArray[i].index, &r);
		if (i != 0) { // not highlighted
			tft.setTextColor(0xFFFF, 0x0000);
		} else { // highlighted
			tft.setTextColor(0x0000, 0xFFFF);
		}
		tft.print(r.name);
		tft.print("\n");
	}
	tft.print("\n");
}

/*
	Moves the highlight to the next position

	Arguments:
		restArray (RestDist*): array of RestDist structs
		x (int): index of current selected restaurant

	Return:
		N/A
*/
void moveHighlight(RestDist* restArray, int x) {
	// get info for old restaurant
	tft.setCursor(0,16*x+1);
	Restaurant oldRest;
	getRestaurant(restArray[x].index, &oldRest);
	// unhighlight old restaurant
	tft.setTextColor(0xFFFF, 0x0000);
	tft.print(oldRest.name);

	// set cursor at new position
	tft.setCursor(0, 16*selectedRest+1);
	Restaurant newRest;
	getRestaurant(restArray[selectedRest].index, &newRest);
	// highlight new restaurant
	tft.setTextColor(0x0000, 0xFFFF);
	tft.print(newRest.name);
}

/*
	Processes Joystick input and controls the cursor accordingly

	Arguments:
		N/A

	Returns: 
		N/A
*/
void joystickMode1() {
	int prevRest = selectedRest;
	int yVal = analogRead(JOYSTICK_VERT);
	selectedRest = constrain(selectedRest, 0, 20);

	// joystick up
	if (yVal < JOY_CENTER - JOY_DEADZONE) {
		selectedRest--;
		Serial.println("Joystick Up");
		if (selectedRest != prevRest && selectedRest > -1) {
			moveHighlight(rest_dist, prevRest);
		}
	}
	// joystick down
	if (yVal > JOY_CENTER + JOY_DEADZONE) {
		selectedRest++;
		Serial.println("Joystick Down");
		if (selectedRest != prevRest && selectedRest < 20) {
			moveHighlight(rest_dist, prevRest);
		}
	}
}

// forward declaration of mode0
void mode0();

/*
	Implementation of mode1 as specified in assignment description

	Arguments:
		N/A

	Returns:
		N/A
*/
void mode1() {
	// load restaurant data into array of RestDist structs
	getRestDist(rest_dist, yegCurrX + cursorX, yegCurrY + cursorY);
	Serial.println("Created RestDist");
	// sort array of RestDist structs by distance
	isort(rest_dist, NUM_RESTAURANTS);
	Serial.println("Sorted");
	// display the list on the screen
	displayNames(rest_dist);
	Serial.println("Displayed");
	// if joystick is moved, scroll through list
	// if joystick is pressed, go back to map display
	while (digitalRead(JOYSTICK_SEL) == HIGH) {
		joystickMode1();
	}
	mode0();
}

/*
	Draws a cursor on the TFT display

	Arguments: 
		colour (uint16_t): the desired TFT color

	Returns: 
		N/A
*/
void redrawCursor(uint16_t colour) {
  tft.fillRect(cursorX - CURSOR_SIZE/2, cursorY - CURSOR_SIZE/2,
               CURSOR_SIZE, CURSOR_SIZE, colour);
}

/*
	Redraws portion of map that cursor has just left to prevent black trail

	Arguments:
		N/A

	Returns: 
		N/a
*/
void redrawMap() {
    int adjustX = prevX - CURSOR_SIZE/2;
    int adjustY = prevY - CURSOR_SIZE/2;
    int mapPosX = yegCurrX + adjustX;
    int mapPosY = yegCurrY + adjustY;
    lcd_image_draw(&yegImage, &tft, mapPosX, mapPosY,
                   adjustX, adjustY,
                   CURSOR_SIZE, CURSOR_SIZE);
}

/* 
	Redraws map patch to fill new display screen

	Arguments:
		xDirection (int): Should be either 1, meaning right, -1, meaning left, or 0 meaning no change
		yDirection (int): Should be either 1, meaning down, or -1, meaning up, or 0 meaning no change
	
	Returns:
		N/A
*/
void drawNextPatch(int xDirection, int yDirection) {

	// adjusts the x position of the map patch drawn
	yegCurrX = yegCurrX + MAP_DISP_WIDTH * (xDirection);
	yegCurrX = constrain(yegCurrX, 0, YEG_X_MAX);

	// adjusts the y position of the map patch drawn
	yegCurrY = yegCurrY + MAP_DISP_HEIGHT * (yDirection);
	yegCurrY = constrain(yegCurrY, 0, YEG_Y_MAX);

	// set the cursor to display in the middle of the screen
	cursorX = MAP_DISP_WIDTH/2;
	cursorY = MAP_DISP_HEIGHT/2;

	// draws next patch
	lcd_image_draw(&yegImage, &tft, 
				   yegCurrX, yegCurrY,
				   0, 0,
				   MAP_DISP_WIDTH, MAP_DISP_HEIGHT);

}

/*
	Draws a new patch of the map based on the cursor position

	Arguments:
		N/A

	Returns:
		N/A
*/
void selectedRestPatch() {
	// get coordinates of selected restaurant
	// then convert those lat/lon coordinates into x/y
	Restaurant currentRest;
	getRestaurant(rest_dist[selectedRest].index, &currentRest);
	int32_t selectedLon = currentRest.lon;
	int32_t selectedLat = currentRest.lat;

	// values of coordinates on map from 0-2048
	int currRestX = lon_to_x(selectedLon);
	int currRestY = lat_to_y(selectedLat);

	// define the middle width/height of display for use in the following functions
	int dispMiddleWidth = MAP_DISP_WIDTH/2;
	int dispMiddleHeight = MAP_DISP_HEIGHT/2;

	// if x and y coordinates are within constraints (not too close to edges)
	// 'normal' case
	if ((currRestX > dispMiddleWidth) && (currRestX < MAP_WIDTH - dispMiddleWidth) 
		&& (currRestY > dispMiddleHeight) && (currRestY < MAP_HEIGHT - dispMiddleHeight)) {

		yegCurrX = currRestX - dispMiddleWidth;
		yegCurrY = currRestY - dispMiddleHeight;

		// draw cursor in middle of screen
	    cursorX = dispMiddleWidth;
	    cursorY = dispMiddleHeight;
	}
	// if not in range of map
	else if (currRestX < 0 || currRestX > MAP_WIDTH || currRestY < 0 || currRestY > MAP_HEIGHT) {
		// location of cursor
		if (currRestX < 0 || currRestX > MAP_WIDTH) {
			cursorX = constrain(currRestX, 0, CURSOR_X_MAX) ;
		}

		if (currRestY < 0 || currRestY > MAP_HEIGHT) {
			cursorY = constrain(currRestY, 0, CURSOR_Y_MAX);
		}

		// draw map
		yegCurrX = constrain(currRestX, 0, YEG_X_MAX);
		yegCurrY = constrain(currRestY, 0, YEG_Y_MAX);
	}
	// if the x coordinate is close to edges
	else {
		cursorX = constrain(currRestX, 0, CURSOR_X_MAX);
		cursorY = constrain(currRestY, 0, CURSOR_Y_MAX);

		yegCurrX = constrain(currRestX, 0, YEG_X_MAX);
		yegCurrY = constrain(currRestY, 0, YEG_Y_MAX);
	}

	// draw the patch of the map with the restaurant located in the middle
	lcd_image_draw(&yegImage, &tft, 
				   yegCurrX, yegCurrY,
				   0, 0,
				   MAP_DISP_WIDTH, MAP_DISP_HEIGHT);	

	// draw cursor
	redrawCursor(TFT_RED);
}

// forward declaration
void restaurantDraw();
void reDrawDots();

/*
	Process touchscreen input

	Arguments:
		N/A

	Returns:
		N/A
*/
void processTouch() {
	TSPoint touch = ts.getPoint();

	// reset pins after reading from touchscreen
	pinMode(YP, OUTPUT); 
	pinMode(XM, OUTPUT); 

	// check that pressure is within acceptable
	if (touch.z < MINPRESSURE || touch.z > MAXPRESSURE) {
		// if it is not, exit the function
		return;
	}
	// if dots are not drawn, draw them
	// if dots are drawn, erase them and redraw map sections
	if (!isDrawn) {
		restaurantDraw();
		isDrawn = true;
	} else {
		reDrawDots();
		isDrawn = false;
	}

	redrawCursor(TFT_RED);
}

/* 
	Processes joystick input for mode 0

	Arguments: 
		N/A

	Returns: 
		N/A
*/
void joystickMode0() {
    int xVal = analogRead(JOYSTICK_HORIZ);
    int yVal = analogRead(JOYSTICK_VERT);

    // stores the previous position of the cursor
    prevX = cursorX;
    prevY = cursorY;

    // change cursorX and cursorY when joystick is moved
    if (yVal < (JOY_CENTER - JOY_DEADZONE)) {
      if (yVal < (JOY_CENTER - JOY_DEADZONE - BUFFER)) {
        cursorY -= 5;
      }
      cursorY -= 1;
    }
    else if (yVal > JOY_CENTER + JOY_DEADZONE) {
      if (yVal > (JOY_CENTER + JOY_DEADZONE + BUFFER)) {
        cursorY += 5;
      }
      cursorY += 1;
    }
    if (xVal > JOY_CENTER + JOY_DEADZONE) {
      if (xVal > JOY_CENTER + JOY_DEADZONE + BUFFER) {
        cursorX -= 5;
      }
      cursorX -= 1;
    }
    else if (xVal < JOY_CENTER - JOY_DEADZONE) {
      if (xVal < JOY_CENTER - JOY_DEADZONE - BUFFER) {
        cursorX += 5;
      }
      cursorX += 1;
    }

    // constrains cursor position to within the map patch displayed
    cursorX = constrain(cursorX, 0, CURSOR_X_MAX);
    cursorY = constrain(cursorY, 0, CURSOR_Y_MAX);

    if (cursorX == CURSOR_X_MAX && yegCurrX < YEG_X_MAX) {
    	drawNextPatch(1, 0);
    } else if (cursorX == 0 && yegCurrX > 0) {
    	drawNextPatch(-1, 0);
    } else if (cursorY == 0 && yegCurrY > 0) {
    	drawNextPatch(0, -1);
    } else if (cursorY == CURSOR_Y_MAX && yegCurrY < YEG_Y_MAX) {
    	drawNextPatch(0, 1);
    }

	// only redraws patch of Edmonton map if cursor position has changed
	// this is to prevent "flickering"
	if (prevX != cursorX || prevY != cursorY) {
	  redrawMap();
	}

	// draw new cursor at new position
	redrawCursor(TFT_RED);
    delay(20);
}

/* 
	Draws a point where each restaurant in range is located

	Arguments: 
		N/A

	Returns:
		N/A
*/
void restaurantDraw() {
	// ensure rest_dist is populated
	getRestDist(rest_dist, yegCurrX + cursorX, yegCurrY + cursorY);
	for (int i = 0; i < NUM_RESTAURANTS; i++) {
		Restaurant currentDrawRest;
		// get latitude and longitude of each restaurant
		getRestaurant(rest_dist[i].index, &currentDrawRest);
		int32_t selectedDrawLon = currentDrawRest.lon;
		int32_t selectedDrawLat = currentDrawRest.lat;
		int currDrawRestX = lon_to_x(selectedDrawLon);
		int currDrawRestY = lat_to_y(selectedDrawLat);
		// only draw the circles if the restaurant is within the map range
		if (currDrawRestX > yegCurrX + 3 && currDrawRestX < yegCurrX + MAP_DISP_WIDTH - 3
			&& currDrawRestY > yegCurrY + 3 && currDrawRestY < yegCurrY + MAP_DISP_HEIGHT - 3) {
			tft.fillCircle(currDrawRestX - yegCurrX, currDrawRestY - yegCurrY, 3, TFT_BLUE);
		}		
	}
}

/* 
	Redraws the map over the restaurant points

	Arguments: 
		N/A

	Returns:
		N/A
*/
void reDrawDots() {
	for (int i = 0; i < NUM_RESTAURANTS; i++) {
		Restaurant currentDrawRest;
		getRestaurant(rest_dist[i].index, &currentDrawRest);
		int32_t selectedDrawLon = currentDrawRest.lon;
		int32_t selectedDrawLat = currentDrawRest.lat;
		int currDrawRestX = lon_to_x(selectedDrawLon);
		int currDrawRestY = lat_to_y(selectedDrawLat);
		if (currDrawRestX > yegCurrX + 3 && currDrawRestX < yegCurrX + MAP_DISP_WIDTH - 5
			&& currDrawRestY > yegCurrY + 3 && currDrawRestY < yegCurrY + MAP_DISP_HEIGHT - 3) {
			// draw the patch of the map covering the circle
			lcd_image_draw(&yegImage, &tft, 
				   currDrawRestX - 3, currDrawRestY - 3,
				   currDrawRestX - yegCurrX - 3, currDrawRestY - yegCurrY - 3,
				   7, 7);
		}		
	}
}

/*
	Implementation of mode 0 as specified in the assignment description

	Arguments:
		N/A

	Returns:
		N/A
*/ 
void mode0() {
	// clear screen
	tft.fillScreen(TFT_BLACK);

	selectedRestPatch();

    while (digitalRead(JOYSTICK_SEL) == HIGH) {
    	joystickMode0();
    	processTouch();
    }

    isDrawn = false;
    selectedRest = 0;
    mode1();
}

void setup() {
	init();

	Serial.begin(9600);

	pinMode(JOYSTICK_SEL, INPUT_PULLUP);

	// read display ID
	uint16_t ID = tft.readID();

	// must come before SD.begin()
	// prepping LCD
	tft.begin(ID);

	// SD card initialization for raw reads
  	Serial.print("Initializing SPI communication for raw reads...");
  	if (!card.init(SPI_HALF_SPEED, SD_CS)) {
  		Serial.println("failed! Is the card inserted properly?");
    	while (true) {}
  	}
  	else {
  		Serial.println("OK!");
  	}

  	// SD card initialization of SD card reads
    Serial.print("Initializing SD card...");
    if (!SD.begin(SD_CS)) {
      Serial.println("failed! Is it inserted properly?");
      while (true) {}
    } else {
    	Serial.println("OK!");
    }

    // sets to correct horizontal orientation
    tft.setRotation(1);

    // resets display to all black
    tft.fillScreen(TFT_BLACK);

    yegCurrX = YEG_MIDDLE_X;
    yegCurrY = YEG_MIDDLE_Y;

    // format text display
	tft.setTextWrap(false);
	tft.setTextSize(2);	
}

int main() {
	setup();
	
	while(true) {
		mode0();		
	}

	Serial.end();
	return 0;
}