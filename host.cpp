#include "cmdlineparser.h"
#include <iostream>
#include <cstring>
#include <chrono>
#include<stdio.h>
#include <stdlib.h>
#include<iostream>
#include <math.h>
#include "ap_fixed.h"

// XRT includes

#include "xrt/xrt_bo.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_kernel.h"

#define SIZE 16

int main(int argc, char** argv) {
    // Command Line Parser
    sda::utils::CmdLineParser parser;

    // Switches
    //**************//"<Full Arg>",  "<Short Arg>", "<Description>", "<Default>"
    parser.addSwitch("--xclbin_file", "-x", "input binary file string", "");
    parser.addSwitch("--device_id", "-d", "device index", "0");
    parser.parse(argc, argv);

    // Read settings
    std::string binaryFile = parser.value("xclbin_file");
    int device_index = stoi(parser.value("device_id"));

    if (argc < 3) {
        parser.printHelp();
        return EXIT_FAILURE;
    }

    std::cout << "Open the device" << device_index << std::endl;
    auto device = xrt::device(device_index);
    std::cout << "Load the xclbin " << binaryFile << std::endl;
    auto uuid = device.load_xclbin(binaryFile);

    auto krnl = xrt::kernel(device, uuid, "aes");

    std::cout << "Allocate Buffer in Global Memory\n";
    auto buf_in = xrt::bo(device, sizeof(uint8_t) * DATA_SIZE, krnl.group_id(0));
    auto buf_out = xrt::bo(device, sizeof(uint8_t) * DATA_SIZE, krnl.group_id(0));

    // Map the contents of the buffer object into host memory
    auto buf_in_map = buf_in.map<uint8_t*>();
    auto buf_out_map = buf_out.map<uint8_t*>();
    
    FILE *input_fd = fopen("input.txt", "rb+");
    FILE *output_fd = fopen("output.txt", "wb+");
    if (input_fd == NULL || output_fd == NULL) {
        // panic
        std::cout << "ERROR: could not open file\n";
        return 1;
    }

    int kern_run_time = 0; // kernel run time in ns

    for (std::size_t r = fread(buf_in_map, sizeof(uint8_t), SIZE, input_fd); r > 0; r = fread(buf_in_map, sizeof(uint8_t), SIZE, input_fd)) {

        // Sync the input buffer to device global memory
        buf_in.sync(XCL_BO_SYNC_BO_TO_DEVICE);

        // execute the kernel
        auto start = std::chrono::steady_clock::now();

        auto run = krnl(buf_in, buf_out);
        run.wait();

        auto end = std::chrono::steady_clock::now();

        running_time += elapsed_ns.count();

        // sync the output buffer
        buf_out.sync(XCL_BO_SYNC_BO_FROM_DEVICE);

        // now write the output into output.txt
        std::size_t w = fwrite(buf_out_map, sizeof(uint8_t), SIZE, output_fd);
        if (r != w) {
            // something probably went wrong, so throw an error...
            std::cout << "ERROR: read and write bytes mismatched\n";
            return 1;
        }
    }

    fclose(input_fd);
    fclose(output_fd);

    std::cout << "Kernel Access Time:" << running_time << " ns\n";


    std::cout << "Comparing output to golden\n";
    uint8_t res_out[SIZE];
    uint8_t gold_out[SIZE];

    FILE *res_fd = fopen("output.txt", "rb+");
    FILE *gold_fd = fopen("output_gold.txt", "rb+");

    if (res_fd == NULL || gold_fd == NULL) {

    }

    std::size_t res_r = fread(res_out, sizeof(uint8_t), SIZE, res_fd);
    std::size_t gold_r = fread(gold_out, sizeof(uint8_t), SIZE, gold_fd);

    while (res_r > 0 && gold_r > 0) {
        if (res_r != gold_r) {
            break;
        }

        for (int i = 0; i < res_r; i++) {
            if (res_out[i] != gold_out[i]) {
                std::cout << "*******************************************\nFAIL: Output DOES NOT match the golden output\n*******************************************\n";
            }
            fclose(res_fd);
            fclose(gold_fd);
            return 1;
        }

        res_r = fread(res_out, sizeof(uint8_t), SIZE, res_fd);
        gold_r = fread(gold_out, sizeof(uint8_t), SIZE, gold_fd);
    }
    fclose(res_fd);
    fclose(gold_fd);

    if (res_r != 0 || gold_r != 0) {
        std::cout << "*******************************************\nFAIL: Output DOES NOT match the golden output\n*******************************************\n";

        return 1;
    }

    // if we reached here, its functionally correct!!
    std::cout << << "*******************************************\nSUCCESS: Output matches the golden output\n*******************************************\n";

    return 0;
}
