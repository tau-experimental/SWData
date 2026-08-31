################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../barker_sync.c \
../carrier_recovery.c \
../channel_monitor.c \
../channel_sim.c \
../clk_detect.c \
../complex_math.c \
../conv_encoder.c \
../costas_loop.c \
../fft_sync.c \
../galois_field.c \
../interleaver.c \
../main.c \
../mls_sync.c \
../modulator.c \
../puncturing.c \
../reed_solomon.c \
../schmidl_cox.c \
../scrambler.c \
../wav_io.c 

C_DEPS += \
./barker_sync.d \
./carrier_recovery.d \
./channel_monitor.d \
./channel_sim.d \
./clk_detect.d \
./complex_math.d \
./conv_encoder.d \
./costas_loop.d \
./fft_sync.d \
./galois_field.d \
./interleaver.d \
./main.d \
./mls_sync.d \
./modulator.d \
./puncturing.d \
./reed_solomon.d \
./schmidl_cox.d \
./scrambler.d \
./wav_io.d 

OBJS += \
./barker_sync.o \
./carrier_recovery.o \
./channel_monitor.o \
./channel_sim.o \
./clk_detect.o \
./complex_math.o \
./conv_encoder.o \
./costas_loop.o \
./fft_sync.o \
./galois_field.o \
./interleaver.o \
./main.o \
./mls_sync.o \
./modulator.o \
./puncturing.o \
./reed_solomon.o \
./schmidl_cox.o \
./scrambler.o \
./wav_io.o 


# Each subdirectory must supply rules for building sources it contributes
%.o: ../%.c subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean--2e-

clean--2e-:
	-$(RM) ./barker_sync.d ./barker_sync.o ./carrier_recovery.d ./carrier_recovery.o ./channel_monitor.d ./channel_monitor.o ./channel_sim.d ./channel_sim.o ./clk_detect.d ./clk_detect.o ./complex_math.d ./complex_math.o ./conv_encoder.d ./conv_encoder.o ./costas_loop.d ./costas_loop.o ./fft_sync.d ./fft_sync.o ./galois_field.d ./galois_field.o ./interleaver.d ./interleaver.o ./main.d ./main.o ./mls_sync.d ./mls_sync.o ./modulator.d ./modulator.o ./puncturing.d ./puncturing.o ./reed_solomon.d ./reed_solomon.o ./schmidl_cox.d ./schmidl_cox.o ./scrambler.d ./scrambler.o ./wav_io.d ./wav_io.o

.PHONY: clean--2e-

