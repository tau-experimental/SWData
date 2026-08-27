################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../complex_filter.c \
../dds.c \
../decoder.c \
../demodulator.c \
../filter.c \
../main.c \
../modulator.c \
../sync.c \
../wav_io.c 

C_DEPS += \
./complex_filter.d \
./dds.d \
./decoder.d \
./demodulator.d \
./filter.d \
./main.d \
./modulator.d \
./sync.d \
./wav_io.d 

OBJS += \
./complex_filter.o \
./dds.o \
./decoder.o \
./demodulator.o \
./filter.o \
./main.o \
./modulator.o \
./sync.o \
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
	-$(RM) ./complex_filter.d ./complex_filter.o ./dds.d ./dds.o ./decoder.d ./decoder.o ./demodulator.d ./demodulator.o ./filter.d ./filter.o ./main.d ./main.o ./modulator.d ./modulator.o ./sync.d ./sync.o ./wav_io.d ./wav_io.o

.PHONY: clean--2e-

