#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>

int main(int argc, char **argv) {
    int soc;
    struct sockaddr_can addr;
    struct ifreq ifr;
    struct can_frame frame_rd;
    int recvbytes;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <interface>\n", argv[0]);
        return 1;
    }

    // 1. Open SocketCAN connection
    if ((soc = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
        fprintf(stderr, "Error while opening socket: %s\n", strerror(errno));
        return 1;
    }

    strcpy(ifr.ifr_name, argv[1]);
    if (ioctl(soc, SIOCGIFINDEX, &ifr) < 0) {
        fprintf(stderr, "Error routing interface: %s\n", strerror(errno));
        close(soc);
        return 1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(soc, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Error binding socket: %s\n", strerror(errno));
        close(soc);
        return 1;
    }

    fprintf(stdout, "[INFO] Successfully connected to %s. Listening for Sorel LTDC frames...\n", argv[1]);

    // 2. Continuous Main Loop
    while (1) {
        // Crucial: Clear out the entire memory space before reading new bytes
        memset(&frame_rd, 0, sizeof(struct can_frame));

        recvbytes = recv(soc, &frame_rd, sizeof(struct can_frame), 0);

        if (recvbytes < 0) {
            if (errno == EINTR) continue; // standard system interrupt handler
            fprintf(stderr, "[DEBUG] Loop broken. Socket error: %s\n", strerror(errno));
            break;
        }

        if (recvbytes < (int)sizeof(struct can_frame)) {
            // Skips incomplete or faulty buffer frames safely without crashing
            continue; 
        }

        time_t timestamp_sec = time(NULL);

        // [DEBUG LOGGING] - Strips network metadata flags to print the clean CAN ID
        unsigned int clean_id = frame_rd.can_id & (CAN_EFF_MASK | CAN_SFF_MASK);
        // fprintf(stdout, "[DEBUG] Received raw CAN ID: 0x%08X (DLC: %d)\n", clean_id, frame_rd.can_dlc);
        
        // 3. Modernised Parser for Sorel Data Streams
        if (clean_id == 0x10017280 || clean_id == 0x10027280 || clean_id == 0x10077280) {
            
            // --- LOOP A: TEMPERATURE SENSORS ---
            if (clean_id == 0x10017280 && frame_rd.can_dlc >= 3) {
                int sensor_index = frame_rd.data[0];
                int16_t val = (frame_rd.data[2] << 8) | frame_rd.data[1];
                
                char sensor[50] = "";
                sprintf(sensor, "Sensor_%d", sensor_index);
                
                // Explicitly map your physical system layout
                if (sensor_index == 0) strcpy(sensor, "collector");
                else if (sensor_index == 1) strcpy(sensor, "tank_bottom");
                else if (sensor_index == 2) strcpy(sensor, "tank_top");

                // If the sensor is truly disconnected, skip it silently
                if (val == 32768 || val == -32768) {
                    continue;
                }

                float temp = (float)val / 10.0;

                // Extra safety: If a sensor gives an impossible reading, log it as diagnostic instead of skipping
                if (temp > 150.0 || temp < -40.0) {
                    fprintf(stdout, "%ld;DLG_DIAG;%s_Invalid_Read;%0.1f\n", timestamp_sec, sensor, temp);
                } else {
                    fprintf(stdout, "%ld;DLG_SENSOR;%s;%0.1f\n", timestamp_sec, sensor, temp);
                }
                fflush(stdout);
            }

            // --- LOOP B: PUMP & RELAY STATES ---
            else if (clean_id == 0x10027280 && frame_rd.can_dlc >= 3) {
                fprintf(stdout, "%ld;DLG_RELAY;Relay_%d;%d\n", timestamp_sec, frame_rd.data[0], frame_rd.data[1]);
                fflush(stdout);
            }

            // --- LOOP C: ENERGY & YIELD METRICS ---
            else if (clean_id == 0x10077280 && frame_rd.can_dlc >= 5) {
                // Sorel logs sub-indexes for energy (data[0]). Let's drop random values and parse the total Wh counter
                if (frame_rd.data[0] == 0) { 
                    long energy_val = (frame_rd.data[4] << 24) | (frame_rd.data[3] << 16) | (frame_rd.data[2] << 8) | frame_rd.data[1];
                    if (energy_val >= 0) {
                        fprintf(stdout, "%ld;DLG_ENERGY;Solar_Yield_Wh;%ld\n", timestamp_sec, energy_val);
                        fflush(stdout);
                    }
                }
            }
        }


    }

    close(soc);
    return 0;
}
