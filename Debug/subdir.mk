################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../complex_math.c \
../conv_encoder.c \
../galois_field.c \
../interleaver.c \
../main.c \
../puncturing.c \
../reed_solomon.c \
../scrambler.c \
../wav_io.c 

C_DEPS += \
./complex_math.d \
./conv_encoder.d \
./galois_field.d \
./interleaver.d \
./main.d \
./puncturing.d \
./reed_solomon.d \
./scrambler.d \
./wav_io.d 

OBJS += \
./complex_math.o \
./conv_encoder.o \
./galois_field.o \
./interleaver.o \
./main.o \
./puncturing.o \
./reed_solomon.o \
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
	-$(RM) ./complex_math.d ./complex_math.o ./conv_encoder.d ./conv_encoder.o ./galois_field.d ./galois_field.o ./interleaver.d ./interleaver.o ./main.d ./main.o ./puncturing.d ./puncturing.o ./reed_solomon.d ./reed_solomon.o ./scrambler.d ./scrambler.o ./wav_io.d ./wav_io.o

.PHONY: clean--2e-

