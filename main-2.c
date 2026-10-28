#include "dtekv-lib.h"

#define SWITCHES ((volatile unsigned int *) 0x04000010)
#define BUTTONS  ((volatile unsigned int *) 0x040000d0)

#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240
#define UI_HEIGHT     22      // top strip with health bars etc

// ---- double buffering ----
// 1 = draw everything into a copy of the screen in the back buffer and copy/swap it
//     to the real screen once per frame with present()
// 0 = draw straight onto the screen 
#define USE_BACK_BUFFER 1

#define VGA_SCREEN ((volatile unsigned char *) 0x08000000)   // the real VGA screen

#if USE_BACK_BUFFER
unsigned char back_buffer[SCREEN_WIDTH * SCREEN_HEIGHT];     // hidden back copy of the screen
//if we use back buffer define the VGA_BUFFER as the back buffer or else define it as the real VGA Screen
#define VGA_BUFFER back_buffer                               
#else
#define VGA_BUFFER VGA_SCREEN                                
#endif

//we can redraw thw whole game onto the back buffer and swap but that is expensive so instead we just redraw the pixels that are amrked dirty and need to be actuall redrawn
#if USE_BACK_BUFFER
int dirty_top = SCREEN_HEIGHT;   // first row that changed since the last present()
int dirty_bottom = -1;           // last row that changed

// remember that rows y_start ... y_end-1 were changed
//used the most in fill_rect() which builds the explosions the tanks, the digits etc etc so we dont see this function explicilty in those but it is still used there
//NK
void mark_dirty(int y_start, int y_end) {
    if (y_start >= y_end) return; //nothing is dirty 
    if (y_start < dirty_top) dirty_top = y_start; //screen grows downward so if we find a y that is smaller we have found a higher redraw y start value
    //-1 for because dirty bottom stores the last row that changed 
    if (y_end - 1 > dirty_bottom) dirty_bottom = y_end - 1; //if we find a higher y IE; a deeper y cordinate than the bottom we update the redraw bottom
}

// copies the changed rows from the back buffer to the screen
//NK
void present() {
    if (dirty_bottom < dirty_top) return;     // nothing changed
    for (int i = dirty_top * SCREEN_WIDTH; i < (dirty_bottom + 1) * SCREEN_WIDTH; i++) {
        VGA_SCREEN[i] = back_buffer[i];
    }
    dirty_top = SCREEN_HEIGHT;
    dirty_bottom = -1;
}
#else
#define mark_dirty(y_start, y_end)
#define present()
#endif

// game settings, change these to tune the game
#define FRAME_DELAY   60000   // bigger number = slower game
#define TANK_W        14
#define TANK_H        8
#define BARREL_LEN    10
#define MAX_HP        100
#define MAX_FUEL      60      // how many pixels you can drive each turn
#define MAX_CLIMB     3       // steepest step a tank can drive up
#define ANGLE_MIN     10
#define ANGLE_MAX     80
#define ANGLE_STEP    5         
#define ANGLE_RATE    4       // frames between angle steps while button is held
#define MAX_SPEED     (6 * 256)   // projectile speed at 100% power (fixed point, 256 = 1 pixel)
#define GRAVITY       20          // added to vertical speed every frame
#define BLAST_RADIUS  20
#define DMG_MAX       40      // damage for a direct hit
#define DMG_MIN       5       // damage at the edge of the blast

// switch modes (SW1 SW0)
#define MODE_SHOOT 0    // 00
#define MODE_LEFT  1    // 01
#define MODE_RIGHT 2    // 10
#define MODE_ANGLE 3    // 11

// game states
#define STATE_AIM    0  // player is moving / aiming
#define STATE_FLIGHT 1  // projectile is in the air
#define STATE_OVER   2  // someone won

// colors, 8 bit: RRRGGGBB
#define SKY      0x7B
#define GRASS    0x1C
#define DIRT     0x88
#define WHITE    0xFF
#define BLACK    0x00
#define GRAY     0x49
#define DARKGRAY 0x25
#define RED      0xE0
#define BLUE     0x03
#define ORANGE   0xF0
#define YELLOW   0xFC
#define CYAN     0x1F
#define MAGENTA  0xE3


// External printing functions provided by the lab environment
extern void print(char *);
extern void print_dec(unsigned int);

// Variables to hold raw counter values
unsigned int c_mcycle;       // Total clock cycles
unsigned int c_minstret;     // Total retired instructions
unsigned int c_mem_ops;      // Memory instructions (loads/stores)
unsigned int c_icache_miss;  // I-cache misses
unsigned int c_dcache_miss;  // D-cache misses
unsigned int c_icache_stall; // I-cache stalls
unsigned int c_dcache_stall; // D-cache stalls
unsigned int c_hazard_stall; // Data hazard stalls
unsigned int c_alu_stall;    // ALU/division stalls

// Clears all 9 performance counters to 0
void clear_counters() {
    asm volatile ("csrw mcycle, x0");
    asm volatile ("csrw minstret, x0");
    asm volatile ("csrw mhpmcounter3, x0");
    asm volatile ("csrw mhpmcounter4, x0");
    asm volatile ("csrw mhpmcounter5, x0");
    asm volatile ("csrw mhpmcounter6, x0");
    asm volatile ("csrw mhpmcounter7, x0");
    asm volatile ("csrw mhpmcounter8, x0");
    asm volatile ("csrw mhpmcounter9, x0");
}

// Reads all 9 counters into C variables
void read_counters() {
    asm volatile ("csrr %0, mcycle"        : "=r"(c_mcycle));
    asm volatile ("csrr %0, minstret"      : "=r"(c_minstret));
    asm volatile ("csrr %0, mhpmcounter3"  : "=r"(c_mem_ops));
    asm volatile ("csrr %0, mhpmcounter4"  : "=r"(c_icache_miss));
    asm volatile ("csrr %0, mhpmcounter5"  : "=r"(c_dcache_miss));
    asm volatile ("csrr %0, mhpmcounter6"  : "=r"(c_icache_stall));
    asm volatile ("csrr %0, mhpmcounter7"  : "=r"(c_dcache_stall));
    asm volatile ("csrr %0, mhpmcounter8"  : "=r"(c_hazard_stall));
    asm volatile ("csrr %0, mhpmcounter9"  : "=r"(c_alu_stall));
}

// Prints the collected data to the console
void print_counter_results() {
    print("\n--- PERFORMANCE COUNTERS ---\n");
    print("Cycles (mcycle):         "); print_dec(c_mcycle);       print("\n");
    print("Instructions (minstret): "); print_dec(c_minstret);     print("\n");
    print("Memory Instr (hpm3):     "); print_dec(c_mem_ops);      print("\n");
    print("I-Cache Misses (hpm4):   "); print_dec(c_icache_miss);  print("\n");
    print("D-Cache Misses (hpm5):   "); print_dec(c_dcache_miss);  print("\n");
    print("I-Cache Stalls (hpm6):   "); print_dec(c_icache_stall); print("\n");
    print("D-Cache Stalls (hpm7):   "); print_dec(c_dcache_stall); print("\n");
    print("Hazard Stalls (hpm8):    "); print_dec(c_hazard_stall); print("\n");
    print("ALU Stalls (hpm9):       "); print_dec(c_alu_stall);    print("\n");
}

// index0 = player 1, index1 = player 2
int tank_x[2];                 // left edge of the tank
int tank_hp[2];                
int tank_angle[2];
int tank_fuel[2];
int tank_dir[2];               // 1 = shoots right, -1 = shoots left
unsigned char tank_color[2];

int terrain[SCREEN_WIDTH];     // y position of the ground for every x its an array of ycords

int turn = 0;                  // whose turn it is tank0 or tank1
int state = STATE_AIM;          
int winner = 0;                // 1 or 2, 0 = draw

int prev_button = 0;           //tracks what the state of the button was last frame used for 
int angle_timer = 0;           // counter for delay the change of the angle by a no of frames 
int angle_direction = 1;       // 1 = angle going up, -1 = going down

int ui_dirty = 1;              // 1 = UI flag needs to be redrawn
int last_mode = -1;            // checks if the UI mode last frame is the same as this frame if not then main will flag it for a redraw

int power = 0;                 // shot power in percent (0, 25, 50, 75 or 100)
int last_power = -1;           // power that was last drawn in the UI

// projectile position and speed, in fixed point (value / 256 = pixels)
int proj_x, proj_y;            //cords of proj 
int proj_vx, proj_vy;          //velocity of the x and y comps of proj 
int proj_old_x, proj_old_y;    // where it was drawn last frame
int proj_age;                  // frames since it was fired
int proj_owner;                // who fired it

// sin(angle) * 256 for 0, 5, 10 ... 90 degrees
// cos(angle) is the same table read backwards, cos(a) = sin(90 - a)
//fixed hardocded sine table for the angle vlaues of sine 
int sin_table[19] = {
    0, 22, 44, 66, 88, 108, 128, 147, 165, 181,
    196, 210, 222, 232, 241, 247, 252, 255, 256
};

// font for digits 0-9, each digit is 3 pixels wide and 5 tall
// every number is one row, the 3 bits are the 3 pixels
// 3x5 bit numbers where a 1 is draw and 0 is dont draw that pixel. 
unsigned char font[10][5] = {
    {7,5,5,5,7},  // 0
    {2,6,2,2,7},  // 1
    {7,1,7,4,7},  // 2
    {7,1,7,1,7},  // 3
    {5,5,7,1,1},  // 4
    {7,4,7,1,7},  // 5
    {7,4,7,5,7},  // 6
    {7,1,1,1,1},  // 7
    {7,5,7,5,7},  // 8
    {7,5,7,1,7}   // 9
};

//handle interuupt leftover from the previous lab
void handle_interrupt(unsigned cause) {
}

//deay function to slow down the game, does nothing for a set no of loops
void delay(int loops) {
    volatile int i;
    for (i = 0; i < loops; i++);
}

//absolute value helper
int abs_value(int a) {
    if (a < 0) return -a;
    return a;
}

// ---------------------------------------------------------------
// drawing
// ---------------------------------------------------------------

// draws a filled rectangle, only on rows >= min_y, and cuts off
// anything outside the screen
void fill_rect(int x, int y, int w, int h, unsigned char color, int min_y) {
    //inintalise the area we clear
    int x_start = x;  
    int x_end = x + w;
    int y_start = y;
    int y_end = y + h;
    //check conditions
    if (x_start < 0) x_start = 0; //if the start cord is out of the left boundary (VGA)
    if (x_end > SCREEN_WIDTH) x_end = SCREEN_WIDTH; //if x cord is out of the right boundary set it to max possible
    if (y_start < min_y) y_start = min_y; // min_y can be 0 or 22, 0 when we draw on the whole screen, 22 when we cannot draw on ui bar, and we can only draw below that area( y cords grow downards)
    if (y_end > SCREEN_HEIGHT) y_end = SCREEN_HEIGHT; //if y ends up outside the screen below it then set it to the screen height
    mark_dirty(y_start, y_end); //mark the are area dirty so that i tcan be swapped 

    //use nested for loops to fill the VGA pixels by pixel and assign it a color we need
    for (int yy = y_start; yy < y_end; yy++) {
        for (int xx = x_start; xx < x_end; xx++) {
            VGA_BUFFER[yy * SCREEN_WIDTH + xx] = color; //array assignmnt
        }
    }
}

// rectangle anywhere on the screen UI bar 
void draw_rect(int x, int y, int w, int h, unsigned char color) {
    fill_rect(x, y, w, h, color, 0); //y_min = 0
}

// rectangle that never draws over the UI strip 
void draw_world_rect(int x, int y, int w, int h, unsigned char color) {
    fill_rect(x, y, w, h, color, UI_HEIGHT); //y_min = 22 = UI_HEIGHT
}

//use nested loops to draw the rectangles that draws number from binary to pixel 
void draw_digit(int x, int y, int digit, int size, unsigned char color) {
    for (int row = 0; row < 5; row++) { //start with vertical row (5rows) 3 bits wide is each row
        for (int col = 0; col < 3; col++) { //start with the msb and then loop to each 
            // check if this pixel of the digit is on by taking the first row, shifting it so that we extract eahc bit of the 3bits every iteration and if it is 1 we draw a rec for that bit 
            if ((font[digit][row] >> (2 - col)) & 1) {
                //x + col for the offset
                draw_rect(x + col * size, y + row * size, size, size, color);
            }
        }
    }
}

void draw_circle(int cx, int cy, int r, unsigned char color) {
    //checks every pixel in a 2r x 2r square for the circle equation
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r * r) { //if we satisfy the circle equation we color that pixel 
                draw_world_rect(cx + dx, cy + dy, 1, 1, color); // and the drawing is done at the cntre +- some offset radius
            }
        }
    }
}

// ---------------------------------------------------------------
// terrain
// ---------------------------------------------------------------

// a wave that goes up and down in a straight line (like a zigzag)
int zigzag(int x, int period, int height) { //x is the horizontal posn and period is width of one zigzag peak
    int t = x % period; // for 1 peak period = 320
    if (t > period / 2) { // were in the second half of peak so we need to go down
        t = period - t; // count downards now 
    }
    return t * 2 * height / period; // return the height (the 2 handles the case where height is max at t = period / 2)
}

// big hill in the middle plus some small bumps
void make_terrain() {
    for (int x = 0; x < SCREEN_WIDTH; x++) {
        //big zigzag increases by 70x160px so 0.44 px per coloumn 
        //small zigzag changes by 8x32px sp 0.25px per coloumn
        terrain[x] = 200 - zigzag(x, 320, 70) - zigzag(x, 64, 8); //create a static hill past period/2 changes sign so a decresing hill can be drawn
    }
}

// what color is the background at this pixel
unsigned char background_color(int x, int y) {
    //if Y is less than (hihger up on screen) than the height asccosicated with the terrain return the pixel as the color of sky (colors the pixel above the terrain as sky at first)
    if (y < terrain[x]) return SKY;
    //if now we are under the terrian but still within a offset of 3 px color it as grass
    if (y < terrain[x] + 3) return GRASS;
    //otherwise it is dirt
    return DIRT;
}

void draw_terrain() {
    //mark the terrain for VGA swap as we are maunally coloring in the entire screen 
    mark_dirty(0, SCREEN_HEIGHT);
    //color in every pixel according to the background_color function logic
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            VGA_BUFFER[y * SCREEN_WIDTH + x] = background_color(x, y);
        }
    }
}

// puts the sky and ground back in an area (this erases things)
void erase_area(int x, int y, int w, int h) {
    //selects the area to erase
    int x_start = x;
    int x_end = x + w;
    int y_start = y;
    int y_end = y + h;
    //same as the fill_rect
    if (x_start < 0) x_start = 0;
    if (x_end > SCREEN_WIDTH) x_end = SCREEN_WIDTH;
    if (y_start < UI_HEIGHT) y_start = UI_HEIGHT;
    if (y_end > SCREEN_HEIGHT) y_end = SCREEN_HEIGHT;
    //mark the erased area dirty for the swap 
    mark_dirty(y_start, y_end);
    //Puts back the orignal color of the pixel at that pixel (SKY,GRASS,DIRT)
    for (int yy = y_start; yy < y_end; yy++) {
        for (int xx = x_start; xx < x_end; xx++) {
            VGA_BUFFER[yy * SCREEN_WIDTH + xx] = background_color(xx, yy);
        }
    }
}

// ---------------------------------------------------------------
// tanks
// ---------------------------------------------------------------

// the tank sits on the highest ground point under it
int tank_top(int p) {
    //sets this as the highest possible y cordinate(bottom)
    int highest = SCREEN_HEIGHT;
    for (int i = 0; i < TANK_W; i++) { // go thorugh each tank pixel from left to right
        int x = tank_x[p] + i; //actual x cord on the screen
        if (x < SCREEN_WIDTH && terrain[x] < highest) { //if we find a smaller y IE higher on the screen we update the highest var
            highest = terrain[x]; //set the highest as the value at that tank cord
        }
    }
    //this returns the total distance from the top of the screen - the tank width which returns the y cordinate of the tank top 
    return highest - TANK_H;
}

void draw_tank(int p) { // p is either 0 or 1
    int top = tank_top(p);

    // pivot point of the barrel
    int pivot_x = tank_x[p] + TANK_W / 2; //calculate the pivot point half the lenght of the rectangle top
    int pivot_y = top - 2; //attach it two pixels above the rectangle 

    int index = tank_angle[p] / 5; //index of the sine table 
    int sin_a = sin_table[index]; 
    int cos_a = sin_table[18 - index];

    // barrel, drawn as a line of small squares 
    for (int k = 2; k <= BARREL_LEN; k++) { 
        int bx = pivot_x + tank_dir[p] * ((cos_a * k) >> 8); //the x cord of the square to draw is at pivot_x + (left or right) + cos_ak  (adjacent side)
        int by = pivot_y - ((sin_a * k) >> 8); //the y cord of the point from where we draw the square same as above but sink as sin(opposite side) 
        draw_world_rect(bx, by, 2, 2, BLACK); //draw a black colored rectangle 
    }

    draw_world_rect(tank_x[p], top, TANK_W, TANK_H, tank_color[p]);      // body
    draw_world_rect(tank_x[p] + 4, top - 3, 6, 3, tank_color[p]);        // turret
    draw_world_rect(tank_x[p], top + TANK_H - 2, TANK_W, 2, GRAY);       // tracks
}

void draw_both_tanks() {
    draw_tank(0);
    draw_tank(1);
}

// erases the tank and its barrel (area is big enough for any angle)
//this will use erase() which will implicitly call mark_dirty
void erase_tank(int p) {
    int top = tank_top(p);
    //big bounding sqaure
    erase_area(tank_x[p] - 4, top - 15, TANK_W + 10, TANK_H + 15);
}

// moves tank p one pixel, d is 1 (right) or -1 (left)
void move_tank(int p, int d) {
    //other tank is 1-p (0 or 1) 
    int other = 1 - p;
    // update tank posn by 1 ahead or 1 behind
    int new_x = tank_x[p] + d;

    if (tank_fuel[p] <= 0) return;                               // out of fuel
    if (new_x < 0 || new_x + TANK_W > SCREEN_WIDTH) return;      // screen edge checks
    if (abs_value(new_x - tank_x[other]) < TANK_W + 4) return;   // if the distance bewteen ply1s potential posn and ply2s position is less than tank width+4(turret included) we return because we cannot move

    //erase the tank for the next frame 
    erase_tank(p);
    //redraw the new tank
    tank_x[p] = new_x;
    tank_fuel[p]--;
    //update the UI and draw the tanks again in case one accidentally deleted the other (too close)
    draw_both_tanks();
    ui_dirty = 1;
}

// ---------------------------------------------------------------
// UI
// ---------------------------------------------------------------

// bar with a white border. if from_right is 1 the bar empties towards the left
void draw_bar(int x, int y, int width, int value, int from_right, unsigned char color) {
    //Draws a white background retangle bar with bigger dimensions than the grey bar 
    draw_rect(x - 1, y - 1, width + 2, 8, WHITE);
    //draws a smaller gray bar to give an white border effect
    draw_rect(x, y, width, 6, DARKGRAY);
    //Safety check if somehow health is bigger than the bar

    if (value > width) value = width;
    //if there is any tank hp left
    if (value > 0) {
        if (from_right) {
            //draw a rectangle at x cordinate + width - health do draw a rectangle so that health emtys towards the right
            draw_rect(x + width - value, y, value, 6, color); //ply2 bar 
        } else {
            draw_rect(x, y, value, 6, color);//plyr 1 bar
        }
    }
}

void draw_ui(int mode) {
    //draw the grey 22 height UI layer
    draw_rect(0, 0, SCREEN_WIDTH, UI_HEIGHT, GRAY);

    // health bars, player 1 on the left and player 2 on the right
    draw_bar(4, 3, 100, tank_hp[0], 0, tank_color[0]);
    draw_bar(216, 3, 100, tank_hp[1], 1, tank_color[1]);

    // box in the color of the player whose turn it is
    draw_rect(139, 2, 42, 8, WHITE);
    draw_rect(140, 3, 40, 6, tank_color[turn]);

    // angle of the current player drawn on the UI
    draw_digit(4, 11, tank_angle[turn] / 10, 2, WHITE);
    draw_digit(12, 11, tank_angle[turn] % 10, 2, WHITE);

    // fuel left
    draw_bar(36, 13, MAX_FUEL, tank_fuel[turn], 0, YELLOW);

    // shot power as a number (0, 25, 50, 75 or 100)
    if (power >= 100) draw_digit(216, 11, 1, 2, WHITE); //condition for power = 100
    if (power >= 10)  draw_digit(224, 11, (power / 10) % 10, 2, WHITE); //condiition for all other nos using tens and ones splitting
    draw_digit(232, 11, power % 10, 2, WHITE);

    // box showing which mode the switches are in
    unsigned char mode_color = MAGENTA;
    if (mode == MODE_SHOOT) mode_color = ORANGE;
    if (mode == MODE_LEFT)  mode_color = CYAN;
    if (mode == MODE_RIGHT) mode_color = YELLOW;
    draw_rect(139, 12, 42, 8, WHITE); //draw mode selector bar
    draw_rect(140, 13, 40, 6, mode_color); //draw colored mode selector bar
}

// big number of the winner in the middle of the screen (0 = draw)
void draw_game_over() {
    unsigned char color = GRAY;
    if (winner == 1) color = RED;
    if (winner == 2) color = BLUE;

    draw_rect(100, 90, 120, 50, WHITE); //draw outline
    draw_rect(104, 94, 112, 42, color); //draw winner colored square 
    draw_digit(151, 100, winner, 6, WHITE); //draw a big digit 
}

// ---------------------------------------------------------------
// game logic
// ---------------------------------------------------------------

// reads the 4 top switches (SW9 SW8 SW7 SW6) and returns the power in percent.
// SW9 = 100, SW8 = 75, SW7 = 50, SW6 = 25. none up = 0.
// if more than one is up, the highest one wins
int read_power() {
    int top_switches = (*SWITCHES >> 6) & 0x0F;   // shift left by 6 and extract the swicthes

    if (top_switches & 0x08) return 100;    // SW9 up
    if (top_switches & 0x04) return 75;     // SW8 up
    if (top_switches & 0x02) return 50;     // SW7 up
    if (top_switches & 0x01) return 25;     // SW6 up
    return 0;                               // none up = no power
}

void new_game() {
    draw_terrain();
    //stats of the plyr1 when a new game is intiiated
    tank_x[0] = 30; //xcord = 30
    tank_dir[0] = 1; //face right
    tank_color[0] = RED;

    tank_x[1] = SCREEN_WIDTH - 30 - TANK_W;//place it on the right side of the screen
    tank_dir[1] = -1; //face left
    tank_color[1] = BLUE; 

    //initialise common stats
    for (int p = 0; p < 2; p++) {
        tank_hp[p] = MAX_HP;
        tank_angle[p] = 45;
        tank_fuel[p] = MAX_FUEL;
    }
    //player 1 turn at a new game
    turn = 0;
    state = STATE_AIM; //start with aiming state
    angle_timer = 0; //delay before update 
    angle_direction = 1; //turret go up when you press at the start
    ui_dirty = 1; //update the UI
    draw_both_tanks(); 
}

void end_turn() { 
    turn = 1 - turn; //change turn to the other player
    tank_fuel[turn] = MAX_FUEL; //reset the fuel for the player whose turn it is 
    state = STATE_AIM; //when you end a turn you have to give back the aiming state to other plyr
    ui_dirty = 1; //update the UI 
}

// projectile hit something at (x, y)
void explode(int x, int y) {
    // explosion animation, growing circles
    for (int r = 3; r <= 12; r += 3) {
        draw_circle(x, y, r, ORANGE);
        draw_circle(x, y, r / 2, YELLOW);
        present(); //prevent buffering as there are a lot of erases and draws 
        delay(FRAME_DELAY); //delay to show growing circles
    }

    // damage both tanks, closer to the center of the blast = more damage (tries to dmaage both tanks)
    for (int p = 0; p < 2; p++) {
        //calcluate the middle x,y of the tank
        int center_x = tank_x[p] + TANK_W / 2;
        int center_y = tank_top(p) + TANK_H / 2;
        //find the distance between where the proj hit and the middle of the tank 
        int dx = center_x - x;
        int dy = center_y - y;
        int dist2 = dx * dx + dy * dy; //dist 2 is hypotenuse squared 
        int radius2 = BLAST_RADIUS * BLAST_RADIUS; //blast raduis squared 

        if (dist2 <= radius2) { //within the damage radius 
            int damage = DMG_MIN + (DMG_MAX - DMG_MIN) * (radius2 - dist2) / radius2; //scale the damage based on the distance between the shot and the tank 
            tank_hp[p] = tank_hp[p] - damage; //update the tank HP
            if (tank_hp[p] < 0) tank_hp[p] = 0; //if HP goes lower than 0 set it back to 0 
        }
    }

    // remove the explosion and draw the tanks again
    erase_area(x - 13, y - 13, 27, 27);
    draw_both_tanks();

    // did someone die?
    if (tank_hp[0] <= 0 || tank_hp[1] <= 0) {
        //kamikaze tank
        if (tank_hp[0] <= 0 && tank_hp[1] <= 0) {
            winner = 0;
        } else if (tank_hp[0] <= 0) {
            winner = 2;
        } else {
            winner = 1;
        }
        state = STATE_OVER;
        draw_ui(0);
        draw_game_over();
    } else {
        end_turn();
    }
}

// shoot a projectile from the end of the barrel
void fire() {
    //extract the angle and the sine and cos values for velocity componenets
    int index = tank_angle[turn] / 5;
    int sin_a = sin_table[index];
    int cos_a = sin_table[18 - index];

    //fibnd the middle pt of the tank 
    int pivot_x = tank_x[turn] + TANK_W / 2;
    int pivot_y = tank_top(turn) - 2; //find the pvot point 
    int tip_x = pivot_x + tank_dir[turn] * ((cos_a * BARREL_LEN) >> 8); //find the tip x of the barrel
    int tip_y = pivot_y - ((sin_a * BARREL_LEN) >> 8); //find the tip y 

    proj_x = tip_x << 8; //shift by 8 to find the actual x cordinate (divide by 8)
    proj_y = tip_y << 8; //same for y 
    int speed = MAX_SPEED * power / 100;         // 25% power = quarter of the speed

    proj_vx = tank_dir[turn] * ((speed * cos_a) >> 8); //x componenet of vlocity 
    proj_vy = -((speed * sin_a) >> 8);           // negative = upwards
    //remember the old x and old y cords of the projectile (it starts from the tip of the barrel)
    proj_old_x = tip_x;
    proj_old_y = tip_y;
    proj_age = 0; //how many frames has it been flying for 
    proj_owner = turn; //who fired it 

    state = STATE_FLIGHT; //transfer to state flight 
}

// called every frame while the projectile is flying
void update_projectile() {
    // erase the projectile from last frame
    erase_area(proj_old_x - 1, proj_old_y - 1, 3, 3);
    draw_both_tanks();      // in case the projectile was drawn over a tank

    // move it
    proj_x = proj_x + proj_vx; //add the change in x direction
    proj_y = proj_y + proj_vy; //add the change in y 
    proj_vy = proj_vy + GRAVITY; //add the effect of gravity 
    proj_age++; //increase the age by 1frame

    int x = proj_x >> 8;    // back to normal pixels
    int y = proj_y >> 8;

    // flew off the side or fell off the bottom = miss
    if (x < 0 || x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT) {
        end_turn();
        return;
    }

    // hit the ground?
    if (y >= 0 && y >= terrain[x]) {
        explode(x, y);
        return;
    }

    // hit a tank? (the shooter is ignored for the first frames so
    // the projectile doesn't hit its own tank)
    for (int p = 0; p < 2; p++) {
        //ignore the first 8 frames
        if (p == proj_owner && proj_age < 8) continue;

        int top = tank_top(p);
        //hitbox condition for hit on the tank
        if (x >= tank_x[p] && x < tank_x[p] + TANK_W && y >= top - 3 && y < top + TANK_H) {
            explode(x, y);
            return;
        }
    }

    // still flying, draw it and save the cords
    proj_old_x = x;
    proj_old_y = y;
    draw_world_rect(x - 1, y - 1, 3, 3, BLACK); //draw from the top left corner 
}

// called every frame while a player is aiming / moving
void update_aim(int mode, int button_long, int button_pressed) {
    if (mode == MODE_SHOOT) {
        if (button_pressed && power > 0) {   // no shot with 0 power
            fire();
        }
    }
    else if (mode == MODE_RIGHT) {
        if (button_long) move_tank(turn, 1);
    }
    else if (mode == MODE_LEFT) {
        if (button_long) move_tank(turn, -1);
    }
    else if (mode == MODE_ANGLE) {
        if (button_long) {
            // while the button is held the angle goes up and down.
            // when you let go it stays at the current value
            angle_timer++; //delay 
            if (angle_timer >= ANGLE_RATE) {
                angle_timer = 0; 

                int new_angle = tank_angle[turn] + angle_direction * ANGLE_STEP; // tang angle +- angle step
                if (new_angle >= ANGLE_MAX) {
                    new_angle = ANGLE_MAX;
                    angle_direction = -1; //invert the direciton
                }
                if (new_angle <= ANGLE_MIN) { //same
                    new_angle = ANGLE_MIN;
                    angle_direction = 1;
                }
                //update the tank 
                erase_tank(turn);
                tank_angle[turn] = new_angle;
                draw_both_tanks();
                ui_dirty = 1;
            }
        } else {
            angle_timer = ANGLE_RATE - 1; //start changing angle when you instantly press the button next time onwards 
        }
    }
}

int main() {
    make_terrain();
    new_game();

    while (1) {
        int mode = *SWITCHES & 0x03;
        int button_long = *BUTTONS & 0x01;

        // button_pressed is only 1 on the frame the button goes down
        int button_pressed = 0;
        if (button_long && !prev_button) { 
            button_pressed = 1;
        }
        prev_button = button_long;

        // power can only be changed while aiming, not while a shot is in the air
        if (state == STATE_AIM) {
            power = read_power();
        }

        if (state == STATE_AIM) {
            update_aim(mode, button_long, button_pressed);
        }
        else if (state == STATE_FLIGHT) {
            update_projectile();
        }
        else if (state == STATE_OVER) {
            if (button_pressed) {
                new_game();     // press the button to play again
            }
        }

        // only redraw the UI when something changed
        if (state != STATE_OVER && (ui_dirty || mode != last_mode || power != last_power)) {
            draw_ui(mode);
            last_mode = mode;
            last_power = power;
            ui_dirty = 0;
        }

        present();
        delay(FRAME_DELAY);
    }

    return 0;
}
