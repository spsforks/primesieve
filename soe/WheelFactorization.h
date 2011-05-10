/*
 * WheelFactorization.h -- This file is part of primesieve
 *
 * Copyright (C) 2011 Kim Walisch, <kim.walisch@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

/** 
 * @file WheelFactorization.h
 * @brief Contains classes and structs related to wheel factorization.
 *
 * Wheel factorization is used to speed-up the sieve of Eratosthenes
 * by skipping multiples of small primes (i.e. 2, 3, 5 and 7 for a
 * modulo 210 wheel).
 * Wheel factorization at the Prime Glossary:
 * http://primes.utm.edu/glossary/xpage/WheelFactorization.html
 */

#ifndef WHEELFACTORIZATION_H
#define WHEELFACTORIZATION_H

#include "defs.h"
#include "pmath.h"

#include <stdexcept>
#include <sstream>
#include <cassert>

/** A sieving prime for use with wheel factorization. */
class WheelPrime {
  friend class EratSmall;
  friend class EratMedium;
public:
  uint32_t getSievePrime() const {
    return sievePrime_;
  }
  uint32_t getSieveIndex() const {
    return index_ & 0x7FFFFF;
  }
  uint32_t getWheelIndex() const {
    return index_ >> 23;
  }
  void setSievePrime(uint32_t sievePrime) {
    sievePrime_ = sievePrime;
  }
  void setSieveIndex(uint32_t sieveIndex) {
    index_ |= sieveIndex;
  }
  void setWheelIndex(uint32_t wheelIndex) {
    index_ = wheelIndex << 23;
  }
private:
  /**
   * A prime prime number <= sqrt(stopNumber) whose multiples will be
   * crossed off in the sieve of Eratosthenes.
   * sievePrime_ = (primeNumber * 2) / 30; *2 is used to skip
   * multiples of 2 and /30 is used as SieveOfEratosthenes uses 30
   * numbers per byte.
   */
  uint32_t sievePrime_;
  /**
   * Contains sieveIndex: 23 bits and wheelIndex: 9 bits, packing
   * wheelIndex and sieveIndex into the same 32 bit word reduces
   * primesieve's memory requirement by 20%.

   * @sieveIndex Determines the position of the current multiple of
   *             sievePrime_ within the SieveOfEratosthenes::sieve_
   *             array.
   * @wheelIndex Is an index for the wheel_ array (of derived
   *             ModuloWheel classes) and is used to calculate the
   *             next multiple of sievePrime_.
   */
  uint32_t index_;
};

/** A container for WheelPrimes for use with singly linked lists. */
template<uint32_t SIZE>
class Bucket {
public:
  Bucket* next;
  void init(Bucket* _next) {
    next = _next;
    count_ = 0;
  }
  void reset() {
    count_ = 0;
  }
  WheelPrime* wheelPrimeBegin() {
    return wheelPrime_;
  }
  WheelPrime* wheelPrimeEnd() {
    return &wheelPrime_[count_];
  }
  /**
   * Adds a wheelPrime to the bucket.
   * @return false if the bucket is full else true.
   */
  bool addWheelPrime(uint32_t sievePrime, uint32_t sieveIndex,
      uint32_t wheelIndex) {
    assert(count_ < SIZE);
    uint32_t pos = count_++;
    WheelPrime& wPrime = wheelPrime_[pos];
    wPrime.setSievePrime(sievePrime);
    wPrime.setWheelIndex(wheelIndex);
    wPrime.setSieveIndex(sieveIndex);
    return (pos != SIZE - 1);
  }
private:
  uint32_t count_;
  WheelPrime wheelPrime_[SIZE];
};

/**
 * Helps to eliminate the current multiple and to calculate the next
 * multiple of the current prime number whilst sieving.
 */
struct WheelElement {
  /**
   * Bitmask used with the '&' operator to unset the bit (of the
   * SieveOfEratosthenes::sieve_ array) corresponding to the
   * current multiple.
   */
  uint8_t unsetBit;
  /**
   * Factor needed to calculate the next multiple of the current
   * prime number (sievePrime).
   */
  uint8_t nextMultipleFactor;
  /** Overflow needed to correct sievePrime * nextMultipleFactor. */
  uint8_t correct;
  /** next wheelIndex = current wheelIndex + next. */
   int8_t next;
};

struct InitWheel {
  uint32_t nextMultipleFactor;
  uint32_t subWheelIndex;
};

/**
 * Used to calculate the initial wheelIndex of a prime number of a 
 * Modulo30Wheel.
 */
extern const InitWheel init30Wheel[30];
/**
 * Used to calculate the initial wheelIndex of a prime number of a
 * Modulo210Wheel.
 */
extern const InitWheel init210Wheel[210];

/**
 * Abstract class that is mainly used to initialize prime numbers for
 * use with wheel factorization.
 */
template<uint32_t WHEEL_MODULO, uint32_t WHEEL_ELEMENTS,
    const InitWheel* INIT_WHEEL>
class ModuloWheel {
private:
  /**
   * Gives the bit position within a byte of the
   * SieveOfEratosthenes::sieve_ array of a prime number. Is used to
   * assign sieving primes to one of eight sub-wheels within the
   * ModuloWheel*::wheel_ array.
   */
  static const uint32_t primeBitPosition_[30];
protected:
  const uint64_t stopNumber_;
  /**
   * @param stopNumber SieveOfEratosthenes::stopNumber_
   * @param sieveSize  SieveOfEratosthenes::sieveSize_
   */
  ModuloWheel(uint64_t stopNumber, uint32_t sieveSize) :
    stopNumber_(stopNumber) {
    uint64_t greatestWheelFactor = INIT_WHEEL[2].nextMultipleFactor;
    // prevents 64 bit overflows of multiple in setWheelPrime()
    if (stopNumber > UINT64_MAX - UINT32_MAX * (greatestWheelFactor + 1)) {
      std::ostringstream error;
      error << "ModuloWheel: stopNumber must be <= (2^64-1) - (2^32-1) * "
          << greatestWheelFactor + 1 << ".";
      throw std::overflow_error(error.str());
    }
    // a sieveSize <= 2^28 allows wheels up to p(17)# without 32 bit
    // overflows of sieveIndex in Erat*::sieve()
    if (sieveSize > (1u << 28))
      throw std::overflow_error("ModuloWheel: sieveSize must be <= 2^28");
  }
  ~ModuloWheel() {
  }
  /**
   * Sets primeNumber, sieveIndex and wheelIndex for use as a
   * wheelPrime.
   * @brief  Calculates the first multiple of primeNumber that needs
   *         to be eliminated and the position within the wheel of
   *         that multiple.
   * @return true if the wheelPrime must be saved for sieving else 
   *         false.
   */
  bool setWheelPrime(uint64_t lowerBound, uint32_t* primeNumber,
      uint32_t* sieveIndex, uint32_t* wheelIndex) {
    // by theory primeNumber^2 is the first multiple of primeNumber
    // that needs to be eliminated
    uint64_t multiple = isquare(*primeNumber);
    if (multiple < lowerBound) {
      // calculate the first multiple >= lowerBound of primeNumber
      multiple = lowerBound + *primeNumber - (lowerBound % *primeNumber);
      // primeNumber not needed for sieving
      if (multiple > stopNumber_)
        return false;
    }
    /// @remark the following if clause is a correction for primes
    /// of type n * 30 + 31
    if (multiple == lowerBound + 1) {
      multiple += *primeNumber;
    }
    uint32_t index = static_cast<uint32_t> (
        (multiple / *primeNumber) % WHEEL_MODULO);
    // get the next multiple that is not divisible by one of the
    // wheel's primes (i.e. 2, 3 and 5 for a modulo 30 wheel)
    multiple += static_cast<uint64_t> (*primeNumber)
        * INIT_WHEEL[index].nextMultipleFactor;
    if (multiple > stopNumber_)
      return false;
    uint32_t subWheelOffset = primeBitPosition_[*primeNumber % 30]
        * WHEEL_ELEMENTS;
    /// @see WheelPrime
    *wheelIndex = INIT_WHEEL[index].subWheelIndex + subWheelOffset;
    /// @remark '- 6' is a correction for primes of type n * 30 + 31
    *sieveIndex = static_cast<uint32_t> (((multiple - lowerBound) - 6) / 30);
    *primeNumber /= 15;
    return true;
  }
};

template<uint32_t WHEEL_MODULO, uint32_t WHEEL_ELEMENTS,
    const InitWheel* INIT_WHEEL>
const uint32_t
    ModuloWheel<WHEEL_MODULO, WHEEL_ELEMENTS, INIT_WHEEL>::primeBitPosition_[30] = { ~0u,
          7, ~0u, ~0u, ~0u, ~0u, ~0u,
          0, ~0u, ~0u, ~0u,   1, ~0u,
          2, ~0u, ~0u, ~0u,   3, ~0u,
          4, ~0u, ~0u, ~0u,   5, ~0u,
        ~0u, ~0u, ~0u, ~0u,   6 };

/**
 * Uses wheel factorization to skip multiples of 2, 3 and 5.
 * Contains the modulo 30 wheel_ array.
 */
class Modulo30Wheel: protected ModuloWheel<30, 8, init30Wheel> {
protected:
#if 0 /* currently not needed */
  static const WheelElement wheel_[8 * 8];
#endif
  Modulo30Wheel(uint64_t stopNumber, uint32_t sieveSize) :
    ModuloWheel<30, 8, init30Wheel> (stopNumber, sieveSize) {
  }
  ~Modulo30Wheel() {
  }
};

/**
 * Uses wheel factorization to skip multiples of 2, 3, 5 and 7.
 * Contains the modulo 210 wheel_ array.
 */
class Modulo210Wheel: protected ModuloWheel<210, 48, init210Wheel> {
protected:
  static const WheelElement wheel_[48 * 8];
  Modulo210Wheel(uint64_t stopNumber, uint32_t sieveSize) :
    ModuloWheel<210, 48, init210Wheel> (stopNumber, sieveSize) {
  }
  ~Modulo210Wheel() {
  }
};

#endif /* WHEELFACTORIZATION_H */
