/*
    This is an example of converting a single frame from an IMX477 camera
    to an RGB frame (saved back to disk as a TGA file). The camera sensor
    provides an image in the following format (in this case):
    
    * Resolution: 1920x1080
    * The pixel format is Bayer RG10, meaning:
        * Each pixel color is max 10 bits wide saved in a 16 bit integer,
          i.e. the values range from 0 to 1023
        * The color format is R G G B, placed in the following way:
          +----+----+----+----+----+----+----+----+----+----+
          | R  | Gr | R  | Gr | R  | Gr | R  | Gr | R  | Gr |
          +----+----+----+----+----+----+----+----+----+----+
          | Gb | B  | Gb | B  | Gb | B  | Gb | B  | Gb | B  |
          +----+----+----+----+----+----+----+----+----+----+
          | R  | Gr | R  | Gr | R  | Gr | R  | Gr | R  | Gr |
          +----+----+----+----+----+----+----+----+----+----+
          | Gb | B  | Gb | B  | Gb | B  | Gb | B  | Gb | B  |
          +----+----+----+----+----+----+----+----+----+----+
        * Gr are green pixels in the red rows, and Gb in the blue rows
        * The output format is a simple G B R, 8-bit per color, not including
          the header:
          +---+---+---+---+---+---+---+---+---+
          | G | B | R | G | B | R | G | B | R |
          +---+---+---+---+---+---+---+---+---+
          | G | B | R | G | B | R | G | B | R |
          +---+---+---+---+---+---+---+---+---+
    
    Since there are two greens for each output pixel, a simple average is
    performed between them, while the red and the blue ones remain their
    value. This will not produce the best results, there are better methods
    out there. Check out this paper:
    https://www.researchgate.net/publication/227014366_Real-time_GPU_color-based_segmentation_of_football_players
    
    The output format is simple BGR bitmap with 8 bits per color. When
    the file is saved, a small TGA header is added so it can be opened in
    any picture viewer or editor.

    Between reading a frame and saving it there's an additional step of
    normalizing it. It should not be necessary, check how it works with
    your images. You can skip this step by removing the call to the
    normalize_frame() function.
*/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#define WIDTH           (1920)                   // Pixels width
#define HEIGHT          (1080)                   // Pixels height

#define RG10_BITS       (10)                     // Bits (max) per input RG10 color, practically will be 16 bits
#define RGB_BITS        (8)                      // Bits per output RGB color
#define MAX_RG10        ((1<<RG10_BITS)-1)       // Max color value
#define MAX_RGB         ((1<<RGB_BITS)-1)        // Max color value

#define RG10_COLOR_SIZE (2)                      // Bytes per RG10 color
#define RGB_COLOR_SIZE  (1)                      // Bytes per RGB color
#define RG10_COLORS     (4)                      // Colors in a RG10 pixel
#define RGB_COLORS      (3)                      // Colors in an RGB pixel

#define RG10_R          (0)                      // Location of the red color in an RG10 pixel
#define RG10_Gr         (RG10_R+1)               // Location of the green color of red row in an RG10 pixel
#define RG10_Gb         (WIDTH*RG10_COLOR_SIZE)  // Location of the green color of blue row in an RG10 pixel
#define RG10_B          (RG10_Gb+1)              // Location of the blue color in an RG10 pixel

#define RGB_R           (2)                      // Location of the red color in an RGB pixel
#define RGB_G           (1)                      // Location of the green color in an RGB pixel
#define RGB_B           (0)                      // Location of the blue color in an RGB pixel

#define RG10_SIZE       (WIDTH*HEIGHT*RG10_COLORS*RG10_COLOR_SIZE) // Total RG10 input frame size
#define RGB_SIZE        (WIDTH*HEIGHT*RGB_COLORS*RGB_COLOR_SIZE) // Total RGB output image size

#define NORM(V)         (V*((float)MAX_RGB/MAX_RG10)) // Normilize a color (V for value) to output size

#define RG10_LOCATION(X, Y, COLOR) (Y*WIDTH*RG10_COLORS+X*RG10_COLOR_SIZE+COLOR) // Location of a pixel in an RG10 frame
#define RGB_LOCATION(X, Y, COLOR)  (Y*WIDTH*RGB_COLORS+X*RGB_COLORS+COLOR) // Location of a pixel in an RGB frame

// Read the file from disk. Doesn't check the file size, using
// a preset frame size of 1920x1080x2x4=16,588,800 bytes.
uint16_t *read_file(char *name)
{
    FILE *file;
    uint16_t *buff;

    buff = (uint16_t *)malloc(RG10_SIZE);

    file = fopen(name, "rb");
    if (!file)
    {
        fprintf(stderr, "Unable to open file %s for reading.\n", name);
        exit(-1);
    }
    fread(buff, 1, RG10_SIZE, file);
    fclose(file);
    return buff;
}

//  Save the output RGB image file with a simple TGA header.
void write_tga(char *name, uint8_t *buff)
{
    FILE *file;
    unsigned char tga_header[18] = {0};

    tga_header[2] = 2;
    tga_header[12] = 255 & WIDTH;
    tga_header[13] = 255 & (WIDTH >> 8);
    tga_header[14] = 255 & HEIGHT;
    tga_header[15] = 255 & (HEIGHT >> 8);
    tga_header[16] = 24;
    tga_header[17] = 32;

    file = fopen(name, "wb");
    if (!file)
    {
        fprintf(stderr, "Unable to open file %s for writing.\n", name);
        exit(-1);
    }
    fwrite(tga_header, sizeof(tga_header), 1, file);
    fwrite(buff, 1, RGB_SIZE, file);
    fclose(file);
}

// Find the min and max values for any of the colors.
void min_max_frame(uint16_t *buffer, uint16_t *min, uint16_t *max)
{
    *max = 0;
    *min = 65535;
    for(int y = 0; y < HEIGHT; y++)
    {
        for(int x = 0; x < WIDTH; x++)
        {
            uint16_t Gb = *(buffer + RG10_LOCATION(x, y, RG10_Gb));
            uint16_t Gr = *(buffer + RG10_LOCATION(x, y, RG10_Gr));
            uint16_t B = *(buffer + RG10_LOCATION(x, y, RG10_B));
            uint16_t R = *(buffer + RG10_LOCATION(x, y, RG10_R));
            if(*max < Gb) *max = Gb;
            if(*max < Gr) *max = Gr;
            if(*max < B) *max = B;
            if(*max < R) *max = R;
            if(*min > Gb) *min = Gb;
            if(*min > Gr) *min = Gr;
            if(*min > B) *min = B;
            if(*min > R) *min = R;           
        }
    }
}

// Normalize the Bayer RG10 frame with min = 0 and max = 1023.
void normalize_frame(uint16_t *buffer)
{
    uint16_t min, max;
    unsigned int location;
    min_max_frame(buffer, &min, &max);
    float mult = 1023 / ((float)max - (float)min);
 
    for(int y = 0; y < HEIGHT; y++)
    {
        for(int x = 0; x < WIDTH; x++)
        {
            location = RG10_LOCATION(x, y, RG10_Gb); *(buffer + location) = round((*(buffer + location) - min) * mult);
            location = RG10_LOCATION(x, y, RG10_Gr); *(buffer + location) = round((*(buffer + location) - min) * mult);
            location = RG10_LOCATION(x, y, RG10_R);  *(buffer + location) = round((*(buffer + location) - min) * mult);
            location = RG10_LOCATION(x, y, RG10_B);  *(buffer + location) = round((*(buffer + location) - min) * mult);
        }
    }
}

// Perform the actual de-Bayering, coverting RGGB to RGB image.
uint8_t *debayer(uint16_t *buffer)
{
    uint8_t *image = malloc(RGB_SIZE);
    for(int y = 0; y < HEIGHT; y++)
    {
        for(int x = 0; x < WIDTH; x++)
        {
            *(image + RGB_LOCATION(x, y, RGB_R)) =  NORM(*(buffer + RG10_LOCATION(x, y, RG10_R)));
            *(image + RGB_LOCATION(x, y, RGB_B)) =  NORM(*(buffer + RG10_LOCATION(x, y, RG10_B)));
            *(image + RGB_LOCATION(x, y, RGB_G)) = NORM((*(buffer + RG10_LOCATION(x, y, RG10_Gb)) +
                                                         *(buffer + RG10_LOCATION(x, y, RG10_Gr))) / 2);
        }
    }
    return image;
}

// The first argument is the input raw file name, the second is the
// output file to save to disk.
int main(int argc, char *argv[])
{
    uint16_t *buffer = read_file(argv[1]);  // Read the frame
    normalize_frame(buffer);                // Normalize it (optional step)
    uint8_t *image = debayer(buffer);       // Debayer
    write_tga(argv[2], image);              // Save back to the disk

    free(buffer);
    free(image);
    return 0;
}
