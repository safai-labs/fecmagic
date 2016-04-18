
// This file is part of fecmagic, the forward error correction library.
// Copyright (c) 2016 Timur Kristóf
// Licensed to you under the terms of the MIT license.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#ifndef CONVOLUTIONAL_DECODER
#define CONVOLUTIONAL_DECODER

#include <cstdint>
#include <limits>
#include <cassert>

#include "fecmagic-global.h"

//#define CONVOLUTIONAL_DECODER_DEBUG
#ifdef CONVOLUTIONAL_DECODER_DEBUG
#   include <iostream>
#   include "binaryprint.h"
#   define DEBUG_PRINT(x) (::std::cout << "[ConvolutionalDecoder] " << x << std::endl);
#else
#   define DEBUG_PRINT(x)
#endif

namespace fecmagic {

    template<uint32_t Depth, uint32_t ConstraintLength, typename TShiftReg = std::uint32_t, TShiftReg ...Polynomials>
    class ConvolutionalDecoder final {
        
        // Check template parameters using static asserts
        static_assert((sizeof(TShiftReg) * 8) >= ConstraintLength, "The shift register must be able to hold the constraint length of the code.");
    
    private:
    
        // Maximum error metric, represents infinity.
        constexpr static TShiftReg maxErrorMetric = ::std::numeric_limits<TShiftReg>::max();
        
        // Number of outputs of the convolutional code (each polynomial corresponds to an output)
        constexpr static uint32_t outputCount_ = sizeof...(Polynomials);
        
        // Unpack variadic template argument, to allow access to each polynomial
        constexpr static TShiftReg polynomials_[sizeof...(Polynomials)] = { Polynomials... };
        
        // Represents a single state that the encoder can be in,
        // used in the decoder for tracing the most likely encoder input.
        class State final {
            
            friend class ConvolutionalDecoder;
            
#ifdef CONVOLUTIONAL_DECODER_DEBUG
            // It is wasteful to store the identifier of the state here,
            // because it is obvious from the loop that works with the states.
            // However it is still useful to track it when debugging.
            TShiftReg state = 0;
#endif
            
            // The presumed input bit that leads to this state
            TShiftReg presumedInputBit;
            
            // The accumulated error metric along the path that leads to this state
            TShiftReg accumulatedErrorMetric;
            
            // The previous state that lead to this state
            const State *previous;
            
            inline explicit State() {
                this->reset();
            }
            
            inline void reset() {
                presumedInputBit = 0;
                accumulatedErrorMetric = maxErrorMetric;
                previous = nullptr;
            }
            
        };
        
        // Represents a step in the decoding procedure
        class Step final {
            
            friend class ConvolutionalDecoder;
            
            // Maximum possible number of states when the current decoder is used.
            constexpr static TShiftReg possibleStateCount = (1 << (ConstraintLength - 1));
            
            // All the possible states in the current decoder step.
            State states[possibleStateCount];
            
            // The lowest error metric found so far in this step.
            TShiftReg lowestErrorMetric = maxErrorMetric;
            
            // The state with the lowest error metric found so far in this step.
            State *lowestErrorState = nullptr;
            
            inline explicit Step() {
                this->reset();
            }
            
            inline void reset() {
                lowestErrorMetric = maxErrorMetric;
                
                for (TShiftReg i = 0; i < possibleStateCount; i++) {
                    this->states[i].reset();
                }
            }
        };
        
        // Gets the output of the convolutional encoder for the given state.
        inline TShiftReg getEncoderOutput(TShiftReg shiftReg) {
            TShiftReg output = 0;
            for (uint32_t o = 0; o < outputCount_; o++) {
                output <<= 1;
                output |= ::fecmagic::computeParity(shiftReg & polynomials_[o]);
            }
            return output;
        }
        
        // Calculates error metric for the given current state assuming the specified presumed input bit.
        inline void calculateErrorMetricForInput(const State &currentState, Step &nextStep, TShiftReg currentSt, TShiftReg receivedBits, uint8_t presumedInputBit) {
            assert(presumedInputBit == (presumedInputBit & 1));
        
            // Next shift register value for the presumed input bit
            TShiftReg nextSr = currentSt | (presumedInputBit << (ConstraintLength - 1));
            TShiftReg nextSt = nextSr >> 1;
            
            // Output of the encoder for the next shift register value
            TShiftReg nextSrOut = getEncoderOutput(nextSr);
            
            // Hamming distance between the would-be encoder output at this state and the received bits
            uint8_t hammingDistance = computeHammingDistance(nextSrOut, receivedBits);
            
            // Calculate error metric
            TShiftReg oldMetric = currentState.accumulatedErrorMetric;
            TShiftReg metric = oldMetric + hammingDistance;
            
            if (metric < oldMetric) {
                // Ooops, overflow, this is bad
                metric = maxErrorMetric;
            }
            
            // Next encoder state for the given presumed input bit
            State &nextState = nextStep.states[nextSt];
            
            DEBUG_PRINT("currstate=" << BinaryPrint<uint8_t>(currentSt) << " nextsr=" << BinaryPrint<uint8_t>(nextSr) << " nextstate=" << BinaryPrint<uint8_t>(nextSt) << " pib=" << (uint32_t)presumedInputBit << " aem=" << (uint32_t)metric << " rbits=" << BinaryPrint<uint8_t>(receivedBits) << " eout=" << BinaryPrint<uint8_t>(nextSrOut) << " hd=" << (uint32_t)hammingDistance);
            if (nextState.accumulatedErrorMetric >= metric) {
                // Overwrite old error metric of this next state with the newly computed one
#ifdef CONVOLUTIONAL_DECODER_DEBUG
                nextState.state = nextSt;
#endif
                nextState.accumulatedErrorMetric = metric;
                nextState.presumedInputBit = presumedInputBit;
                nextState.previous = &currentState;
                
                if (metric < nextStep.lowestErrorMetric) {
                    nextStep.lowestErrorMetric = metric;
                    nextStep.lowestErrorState = &nextState;
                }
            }
        }
    
    public:
        /**
          * @brief Default constructor.
          */
        constexpr inline explicit ConvolutionalDecoder() { }
        
        /**
          * @brief Copy constructor. Intentionally disabled for this class.
          */
        ConvolutionalDecoder(const ConvolutionalDecoder &other) = delete;
        
        /**
          * @brief Move constructor.
          */
        ConvolutionalDecoder(const ConvolutionalDecoder &&other) { }
        
        /**
          * @brief Copy assignment operator. Intentionally disabled for this class.
          */
        ConvolutionalDecoder &operator=(const ConvolutionalDecoder &other) = delete;
        
        /**
          * @brief Move assignment operator.
          */
        ConvolutionalDecoder &operator=(const ConvolutionalDecoder &&other) { };
        
        /**
          * @brief Returns the reciproc of the code rate.
          */
        constexpr inline uint32_t reciprocCodeRate() const {
            return outputCount_;
        }
        
        /**
         * @brief Decodes a given block.
         *
         * Decodes the block of supplied input bytes using the current convolutional code.
         * The caller of this method is responsible for making sure that enough memory is
         * allocated to fit the output.
         *
         * This method is not suitable for streaming.
         */
        void decodeBlock(const uint8_t *input, size_t inputSize, uint8_t *output) {
            // Check parameters
            if (inputSize == 0) {
                return;
            }
            assert(input != nullptr);
            assert(output != nullptr);
            
            DEBUG_PRINT("depth=" << (uint32_t)Depth << ", possibleStateCount=" << (uint32_t)Step::possibleStateCount);
        
            // All the state that we need to keep in memory at once
            Step window[Depth];
            
            uint32_t windowPos = 0;
            
            // Input position
            size_t inAddr = 0;
            uint32_t inBitPos = 7;
            
            // Output position
            size_t outAddr = 0;
            uint32_t outBitPos = 7;
            
            // The encoder always starts at the 0 state, so the error metric of that is 0
            window[windowPos].states[0].accumulatedErrorMetric = 0;
            window[windowPos].lowestErrorMetric = 0;
            window[windowPos].lowestErrorState = &window[windowPos].states[0];
            
            uint32_t nextWindowPos = 0;
            uint32_t afterNextWindowPos = 0;
            uint32_t currentStepCount = 0;
            
            while (inAddr < inputSize) {
                TShiftReg receivedBits = 0;
                
                // Get necessary number of input bits
                for (uint32_t o = 0; o < outputCount_; o++) {
                    // Read current input bit
                    receivedBits <<= 1;
                    receivedBits |= ((input[inAddr] >> inBitPos) & 1);
                    
                    // Advance input bit position
                    if (inBitPos == 0) {
                        inAddr++;
                        inBitPos = 7;
                    }
                    else {
                        inBitPos--;
                    }
                }
                
                // Calculate next position in the window
                if (windowPos == (Depth - 1)) {
                    nextWindowPos = 0;
                }
                else {
                    nextWindowPos = windowPos + 1;
                }
                
                // Go through all possible states at current step
                for (TShiftReg i = 0; i <  Step::possibleStateCount; i++) {
                    // Current state
                    State &currentState = window[windowPos].states[i];
                    
                    // If accumulated metric is infinity, we don't bother with this state
                    if (currentState.accumulatedErrorMetric == maxErrorMetric) {
                        continue;
                    }
#ifdef CONVOLUTIONAL_DECODER_DEBUG
                    currentState.state = i;
#endif
                    
                    // Calculate appropriate error metric for possible input bits
                    calculateErrorMetricForInput(currentState, window[nextWindowPos], i, receivedBits, 0);
                    calculateErrorMetricForInput(currentState, window[nextWindowPos], i, receivedBits, 1);
                }
                
                
                if (currentStepCount > (Depth - 2)) {
                    // Get output bits, if any, by tracing back
                    const State *stateWithOutput = window[nextWindowPos].lowestErrorState;
                    for (uint32_t i = 0; i < (Depth - 1); i++) {
                        stateWithOutput = stateWithOutput->previous;
                    }
                    
                    // Get output bit and put it to its correct place
                    uint8_t pib = stateWithOutput->presumedInputBit;
                    assert((pib & 1) == pib);
                    
                    DEBUG_PRINT("<--- pib_out=" << ((uint32_t)pib) << " state=" << BinaryPrint<uint8_t>(stateWithOutput->state));
                    output[outAddr] |= (pib << outBitPos);
                    
                    // Advance output bit position
                    if (outBitPos == 0) {
                        outAddr++;
                        outBitPos = 7;
                    }
                    else {
                        outBitPos--;
                    }
                }
                
                // Get the window position after the next one
                if (nextWindowPos == (Depth - 1)) {
                    afterNextWindowPos = 0;
                }
                else {
                    afterNextWindowPos = nextWindowPos + 1;
                }
                
                // Reset the step after the next one, so that it can start fresh
                window[afterNextWindowPos].reset();
                
                // Advance window position
                windowPos = nextWindowPos;
                
                // Increment step counter
                currentStepCount ++;
            }
            
            // We ran out of inputs.
            // Let's get the remaining output bits, if any, by tracing back
            uint8_t remainingOutputBits[Depth - 1];
            const State *stateWithOutput = window[windowPos].lowestErrorState;
            
            // Go through the state with lowest error metric backwards, this will go through them in reversed order
            uint32_t tracebackDepth;
            if (currentStepCount > (Depth - 1)) {
                tracebackDepth = Depth - 1;
            }
            else {
                tracebackDepth = currentStepCount;
            }
            
            uint32_t trackbackIndex = tracebackDepth;
            for (; trackbackIndex > 0; trackbackIndex--) {
                DEBUG_PRINT("at the end, got out bit: " << (uint32_t)stateWithOutput->presumedInputBit << " state=" << (uint32_t)stateWithOutput->state);
                
                // Useful for debugging when decoding without bit errors
                //assert(stateWithOutput->accumulatedErrorMetric == 0);
                
                remainingOutputBits[trackbackIndex - 1] = stateWithOutput->presumedInputBit;
                stateWithOutput = stateWithOutput->previous;
            }
            
            // Put the remaining bits to the output in the correct order
            for (; trackbackIndex < tracebackDepth; trackbackIndex++) {
                uint8_t pib = remainingOutputBits[trackbackIndex];
                assert((pib & 1) == pib);
                
                DEBUG_PRINT("<--- pib_out=" << ((uint32_t)pib));
                output[outAddr] |= (pib << outBitPos);
                
                // Advance output bit position
                if (outBitPos == 0) {
                    outAddr++;
                    outBitPos = 7;
                }
                else {
                    outBitPos--;
                }
            }
            
        }
    
    
    };

}

#ifdef DEBUG_PRINT
#   undef DEBUG_PRINT
#endif

#endif // CONVOLUTIONAL_DECODER

