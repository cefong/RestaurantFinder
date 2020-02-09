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
#define LAT_NORTH 53618581
#define LAT_SOUTH 53409531
#define LON_WEST -113686521
#define LON_EAST -113334961

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

// restaurant struct, from weekly exercise
struct Restaurant {
	int32_t lat;
	int32_t lon;
	uint8_t rating; // from 0 to 10
	char name[55];
};

// restDist struct, stores index and distance from current 
// cursor location. Can use index to pull info from corresponding
// Restaurant struct
struct RestDist {
	uint16_t index; // index of restaurant from 0 to NUM_RESTAURANTS-1
	uint16_t dist; // Manhattan distance to cursor position
};

// This is my code for Mode 1.
// Initialize global variables oldBlock and restBlock used in the fast method
uint32_t oldBlock = 0;
Restaurant restBlock[8];
// array containing 1066 RestDist structures
RestDist rest_dist[NUM_RESTAURANTS];
// selectedRest needs to change with joystick movement
int selectedRest = 0;

// Mode 0

// current cursor position on the display
int cursorX, cursorY;

// previous cursor position on the display
int prevX, prevY;

// coordinates to draw middle of map
int yegMiddleX, yegMiddleY;

// coordinates of current point on map
int yegCurrX, yegCurrY;

// forward declaration for redrawing cursor
void redrawCursor(uint16_t colour);
 
void setup() {
  	init();
  	pinMode(JOYSTICK_SEL, INPUT);
  	digitalWrite(JOYSTICK_SEL, HIGH);
  	Serial.begin(9600);

  	// tft display initialization
  	uint16_t ID = tft.readID();
  	tft.begin(ID);

  	// SD card initialization of SD card reads
    Serial.print("Initializing SD card...");
    if (!SD.begin(SD_CS)) {
      Serial.println("failed! Is it inserted properly?");
      while (true) {}
    }

  	// SD card initialization for raw reads
  	Serial.print("Initializing SPI communication for raw reads...");
  	if (!card.init(SPI_HALF_SPEED, SD_CS)) {
  		Serial.println("failed! Is the card inserted properly?");
    	while (true) {}
  	}
  	else {
  		Serial.println("OK!");
  	}

  	tft.fillScreen(TFT_BLACK);
  	tft.setRotation(1);

  	// set initial map patch to be drawn
    yegMiddleX = MAP_WIDTH/2 - MAP_DISP_WIDTH/2;
    yegMiddleY = MAP_HEIGHT/2 - MAP_DISP_HEIGHT/2;

    yegCurrX = yegMiddleX;
    yegCurrY = yegMiddleY;
}

void redrawCursor(uint16_t colour) {
  tft.fillRect(cursorX - CURSOR_SIZE/2, cursorY - CURSOR_SIZE/2,
               CURSOR_SIZE, CURSOR_SIZE, colour);
}

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

// made my own absolute value function because cmath didn't work lol
uint16_t absolute(uint32_t n) {
	if (n < 0) {
		n = n * (-1);
	}
	uint16_t j = n;
	return j;
}


// swaps two RestDist structures (used in insertion sort)
void swap(RestDist &x,RestDist &y) {
	RestDist temp = x;
	x = y;
	y = temp;
}


// insertion sort function, got this from assignment description
// sorts by distance from cursor
void isort(RestDist* distArray, int length) {
	int i = 1;
	while (i < length) {
		int j = i;
		while (j > 0 && distArray[j-1].dist > distArray[j].dist) {
			swap(distArray[j], distArray[j-1]);
			--j;
		}
		i++;
	}
}

/* Retrieves a new restaurant block only if the current restaurant is not
in the current block.

Arguments:
	restIndex: The index of the restaurant (0 to 1065)
	restPtr: Points to the restaurant address
*/
// this is the fast getRestaurant from WE1
void getRestaurant(int restIndex, Restaurant* restPtr) {
	uint32_t blockNum = REST_START_BLOCK + restIndex/8;
	//restaurant restBlock[8];

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


// Caclulates Manhattan Distance from cursor location
uint16_t manhattanDist(int32_t x1,int32_t x2,int32_t y1,int32_t y2) {
	uint16_t dist = absolute(x1 - x2) + absolute(y1 - y2);
	return dist;
}


// Gets an array of RestDist structures based on current location
void getRestDist(RestDist* restDistArray, int16_t x, int16_t y) {
	// load array of RestDist structures
	Restaurant rest;
	for (int i = 0; i < NUM_RESTAURANTS; i++) {
		// load 1 restaurant from SD card
		getRestaurant(i, &rest);
		restDistArray[i].index = i;
		// store index and distance as RestDist struct in rest_dist array
		int32_t lat = rest.lat;
		int32_t lon = rest.lon;
		int32_t xLon = x_to_lon(x);
		int32_t yLat = y_to_lat(y);
		restDistArray[i].dist = manhattanDist(xLon, lon, yLat, lat);
	}
}


// Displays the initial list of restaurants
void displayNames(RestDist* restArray) {
	tft.fillScreen(0);
	tft.setCursor(0,0);

	for (int16_t i = 0; i < 21; i++) {
		Restaurant r;
		getRestaurant(restArray[i].index, &r);
		if (i != 0) { // not highlighted
			tft.setTextColor(0xFFFF, 0x0000);
		} else { // highlighted
			// black characters on white background
			tft.setTextColor(0x0000, 0xFFFF);
		}
		tft.print(r.name);
		tft.print("\n");
	}
	tft.print("\n");
}

// moves the highlight to the next position
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

// Processes Joystick input and controls the cursor accordingly.
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

void mode1() {
	// 1. load restaurant data into array of RestDist structs
		// calculate manhattan distance for each restaurant
	getRestDist(rest_dist, yegCurrX + cursorX, yegCurrY + cursorY);
	Serial.println("Created RestDist");
	// 2. sort array of RestDist structs by distance
	isort(rest_dist, NUM_RESTAURANTS);
	Serial.println("Sorted");
	// 3. display the list on the screen
	displayNames(rest_dist);
	Serial.println("Displayed");
	// 4. if joystick is moved, scroll through list
	// 5. if joystick is pressed, go back to map display
	while (digitalRead(JOYSTICK_SEL) == HIGH) {
		joystickMode1();
	}
	mode0();
}

// functions needed for mode0

// the following functions adapted from weekly exercise #1

// redraws portion of map that cursor has just left to prevent black trail
void redrawMap() {
    int adjustX = prevX - CURSOR_SIZE/2;
    int adjustY = prevY - CURSOR_SIZE/2;
    int mapPosX = yegCurrX + adjustX;
    int mapPosY = yegCurrY + adjustY;
    lcd_image_draw(&yegImage, &tft, mapPosX, mapPosY,
                   adjustX, adjustY,
                   CURSOR_SIZE, CURSOR_SIZE);
}

// handle what to do when cursor hits map edge
void drawNextPatchRight() {
	yegCurrX = yegCurrX + MAP_DISP_WIDTH;
	yegCurrX = constrain(yegCurrX, 0, MAP_WIDTH - MAP_DISP_WIDTH);
	lcd_image_draw(&yegImage, &tft, 
				   yegCurrX, yegCurrY,
				   0, 0,
				   MAP_DISP_WIDTH, MAP_DISP_HEIGHT);
}

void drawNextPatchLeft() {
	yegCurrX = yegCurrX - MAP_DISP_WIDTH;
	yegCurrX = constrain(yegCurrX, 0, MAP_WIDTH - MAP_DISP_WIDTH);
	lcd_image_draw(&yegImage, &tft, 
				   yegCurrX, yegCurrY,
				   0, 0,
				   MAP_DISP_WIDTH, MAP_DISP_HEIGHT);	
}

void drawNextPatchDown() {
	yegCurrY = yegCurrY + MAP_DISP_HEIGHT;
	yegCurrY = constrain(yegCurrY, 0, MAP_HEIGHT - MAP_DISP_HEIGHT);
	lcd_image_draw(&yegImage, &tft, 
				   yegCurrX, yegCurrY,
				   0, 0,
				   MAP_DISP_WIDTH, MAP_DISP_HEIGHT);
}

void drawNextPatchUp() {
	yegCurrY = yegCurrY - MAP_DISP_HEIGHT;
	yegCurrY = constrain(yegCurrY, 0, MAP_HEIGHT - MAP_DISP_HEIGHT);
	lcd_image_draw(&yegImage, &tft, 
				   yegCurrX, yegCurrY,
				   0, 0,
				   MAP_DISP_WIDTH, MAP_DISP_HEIGHT);
}

void joystickMode0() {
    int xVal = analogRead(JOYSTICK_HORIZ);
    int yVal = analogRead(JOYSTICK_VERT);
    int buttonVal = digitalRead(JOYSTICK_SEL);

    // stores the previous position of the cursor
    prevX = cursorX;
    prevY = cursorY;

    // now move the cursor
    if (yVal < (JOY_CENTER - JOY_DEADZONE)) {
      if (yVal < (JOY_CENTER - JOY_DEADZONE - BUFFER)) {
        cursorY -= 5;
      }
      cursorY -= 1; // decrease the y coordinate of the cursor
    }
    else if (yVal > JOY_CENTER + JOY_DEADZONE) {
      if (yVal > (JOY_CENTER + JOY_DEADZONE + BUFFER)) {
        cursorY += 5;
      }
      cursorY += 1;
    }

    // remember the x-reading increases as we push left
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
    cursorX = constrain(cursorX, CURSOR_SIZE/2, (DISPLAY_WIDTH - 61 - CURSOR_SIZE/2));
    cursorY = constrain(cursorY, CURSOR_SIZE/2, (DISPLAY_HEIGHT - CURSOR_SIZE/2));

    if (cursorX == (MAP_DISP_WIDTH - 1 - CURSOR_SIZE/2) && yegCurrX < (MAP_WIDTH - MAP_DISP_WIDTH)) {
    	drawNextPatchRight();
        // initial cursor position is the middle of the screen
    	cursorX = MAP_DISP_WIDTH/2;
    	cursorY = MAP_DISP_HEIGHT/2;
    } else if (cursorX == (CURSOR_SIZE/2) && yegCurrX > 0) {
    	drawNextPatchLeft();
    	// inital cursor in middle of screen
    	cursorX = MAP_DISP_WIDTH/2;
    	cursorY = MAP_DISP_HEIGHT/2;
    } else if (cursorY == (CURSOR_SIZE/2) && yegCurrY > 0) {
    	drawNextPatchUp();
    	// initial cursor in middle of screen
    	cursorX = MAP_DISP_WIDTH/2;
    	cursorY = MAP_DISP_HEIGHT/2;
    } else if (cursorY == (DISPLAY_HEIGHT - CURSOR_SIZE/2) && yegCurrY < (MAP_HEIGHT - MAP_DISP_HEIGHT)) {
    	drawNextPatchDown();
    	// initial cursor
    	cursorX = MAP_DISP_WIDTH/2;
    	cursorY = MAP_DISP_HEIGHT/2;
    }
	// only redraws patch of Edmonton map if cursor position has changed
	// this is to prevent "flickering"
	if (prevX != cursorX or prevY != cursorY) {
	  redrawMap();
	}

	// draw new cursor at new position
	redrawCursor(TFT_RED);
    delay(20);
}

// draw patch of screen for restaurant
void selectedRestPatch(RestDist array) {
	// call this with rest_dist (array storing all restaurants and their distances and indices)
	// need to get coordinates of selected restaurant
	// then need to convert those lat/lon coordinates into x/y
	Restaurant currentRest;
	getRestaurant(array[selectedRest].index, &currentRest);
	int32_t selectedLon = currentRest.lon;
	int32_t selectedLat = currentRest.lat;

	yegCurrX = lon_to_x(selectedLon);
	yegCurrY = lat_to_y(selectedLat);

	// if the x coordinate of the restaurant is >210px from either edge and
	// y coordinate is more that 160 px away from top and bottom, 
	// implement the cursor being at the location of the restaurant, both of which display
	// at the center of the screen (i.e. take lat-mapdisplayheight/2 and long - mapdisplayheight/2)

	// if the x coordinate is within 210 px from either edgeand
	// y coordinate is more than 160 px away from top and bottom
	//implement cursor at location of restaurant but map patch drawn constrained to 
	// mapheight - mapdisplay height, and mapwidth - mapdisplaywidth

	// if not in range of map,
	// put cursor on closest location to restaurant. if it is outside x constraint, keep the
	// latitude and put cursor on edge of screen, vice versa.
}
void mode0() {
	// clear screen
	tft.fillScreen(TFT_BLACK);
    // draws the centre of the Edmonton map, leaving the rightmost 60 columns black
    lcd_image_draw(&yegImage, &tft, 
    			   yegCurrX, yegCurrY,
                   0, 0,
                   MAP_DISP_WIDTH, MAP_DISP_HEIGHT);

    // initial cursor position is the middle of the screen
    cursorX = MAP_DISP_WIDTH/2;
    cursorY = MAP_DISP_HEIGHT/2;

    redrawCursor(TFT_RED);
    while (digitalRead(JOYSTICK_SEL) == HIGH) {
    	joystickMode0();
    }
    selectedRest = 0;
    mode1();
}

int main() {
	setup();
	// display list of restaurants that are closest to the chosen position
		// 21 restaurants
		// text size of 2
		// sort by Manhattan distance
		// d((x1,y1),(x2,y2)) = |x1 - x2| + |y1 - y2|
	// user can use joystick to choose a restaurant (highlight block)
	// when user pushes the button again, the program will go back to mode 0
	// but with the selected restaurant at the centre
	// IMPLEMENT THIS CODE WHEN JOYSTICK BUTTON IS PRESSED
	// xPos, yPos = position of cursor when pressed
	tft.setTextWrap(false);
	tft.setTextSize(2);
	while (true) {
		mode0();
	}
	// when the user presses the joystick, enter mode 1
	// replace the following with code that would display map
	// etc, just filled screen for testing the joystick button

	Serial.end();

	return 0;
}