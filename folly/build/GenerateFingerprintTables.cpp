/*
 * Copyright 2017 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS 1
#endif

#include <cstdio>
#include <cinttypes>

#include <string>

#include <glog/logging.h>

#include <folly/Format.h>
#include <folly/portability/GFlags.h>

#include <folly/detail/FingerprintPolynomial.h>

using namespace folly;
using namespace folly::detail;

// The defaults were generated by a separate program that requires the
// NTL (Number Theory Library) from http://www.shoup.net/ntl/
//
// Briefly: randomly generate a polynomial of degree D, test for
// irreducibility, repeat until you find an irreducible polynomial
// (roughly 1/D of all polynomials of degree D are irreducible, so
// this will succeed in D/2 tries on average; D is small (64..128) so
// this simple method works well)
//
// DO NOT REPLACE THE POLYNOMIALS USED, EVER, as that would change the value
// of every single fingerprint in existence.
DEFINE_int64(poly64, 0xbf3736b51869e9b7,
             "Generate 64-bit tables using this polynomial");
DEFINE_int64(poly96_m, 0x51555cb0aa8d39c3,
             "Generate 96-bit tables using this polynomial "
             "(most significant 64 bits)");
DEFINE_int32(poly96_l, 0xb679ec37,
             "Generate 96-bit tables using this polynomial "
             "(least significant 32 bits)");
DEFINE_int64(poly128_m, 0xc91bff9b8768b51b,
             "Generate 128-bit tables using this polynomial "
             "(most significant 64 bits)");
DEFINE_int64(poly128_l, 0x8c5d5853bd77b0d3,
             "Generate 128-bit tables using this polynomial "
             "(least significant 64 bits)");
DEFINE_string(install_dir, ".",
              "Direectory to place output files in");
DEFINE_string(fbcode_dir, "", "fbcode directory (ignored)");

namespace {

template <int DEG>
void computeTables(FILE* file, const FingerprintPolynomial<DEG>& poly) {
  uint64_t table[8][256][FingerprintPolynomial<DEG>::size()];
  // table[i][q] is Q(X) * X^(k+8*i) mod P(X),
  // where k is the number of bits in the fingerprint (and deg(P)) and
  // Q(X) = q7*X^7 + q6*X^6 + ... + q1*X + q0 is a degree-7 polyonomial
  // whose coefficients are the bits of q.
  for (int x = 0; x < 256; x++) {
    FingerprintPolynomial<DEG> t;
    t.setHigh8Bits(x);
    for (int i = 0; i < 8; i++) {
      t.mulXkmod(8, poly);
      t.write(&(table[i][x][0]));
    }
  }

  // Write the actual polynomial used; this isn't needed during fast
  // fingerprint calculation, but it's useful for reference and unittesting.
  uint64_t poly_val[FingerprintPolynomial<DEG>::size()];
  poly.write(poly_val);
  CHECK_ERR(fprintf(file,
      "template <>\n"
      "const uint64_t FingerprintTable<%d>::poly[%d] = {",
      DEG+1, FingerprintPolynomial<DEG>::size()));
  for (int j = 0; j < FingerprintPolynomial<DEG>::size(); j++) {
    CHECK_ERR(fprintf(file, "%s%" PRIu64 "LU", j ? ", " : "", poly_val[j]));
  }
  CHECK_ERR(fprintf(file, "};\n\n"));

  // Write the tables.
  CHECK_ERR(fprintf(file,
      "template <>\n"
      "const uint64_t FingerprintTable<%d>::table[8][256][%d] = {\n",
      DEG+1, FingerprintPolynomial<DEG>::size()));
  for (int i = 0; i < 8; i++) {
    CHECK_ERR(fprintf(file,
        "  // Table %d"
        "\n"
        "  {\n", i));
    for (int x = 0; x < 256; x++) {
      CHECK_ERR(fprintf(file, "    {"));
      for (int j = 0; j < FingerprintPolynomial<DEG>::size(); j++) {
        CHECK_ERR(fprintf(
          file, "%s%" PRIu64 "LU", (j ? ", " : ""), table[i][x][j]));
      }
      CHECK_ERR(fprintf(file, "},\n"));
    }
    CHECK_ERR(fprintf(file, "  },\n"));
  }
  CHECK_ERR(fprintf(file, "\n};\n\n"));
}

}  // namespace

int main(int argc, char *argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);

  std::string name = folly::format("{}/{}", FLAGS_install_dir,
                                   "FingerprintTables.cpp").str();
  FILE* file = fopen(name.c_str(), "w");
  PCHECK(file);

  CHECK_ERR(fprintf(file,
      "/**\n"
      " * Fingerprint tables for 64-, 96-, and 128-bit Rabin fingerprints.\n"
      " *\n"
      " * AUTOMATICALLY GENERATED.  DO NOT EDIT.\n"
      " */\n"
      "\n"
      "#include <folly/Fingerprint.h>\n"
      "\n"
      "namespace folly {\n"
      "namespace detail {\n"
      "\n"));

  FingerprintPolynomial<63> poly64((const uint64_t*)&FLAGS_poly64);
  computeTables(file, poly64);

  uint64_t poly96_val[2];
  poly96_val[0] = (uint64_t)FLAGS_poly96_m;
  poly96_val[1] = (uint64_t)FLAGS_poly96_l << 32;
  FingerprintPolynomial<95> poly96(poly96_val);
  computeTables(file, poly96);

  uint64_t poly128_val[2];
  poly128_val[0] = (uint64_t)FLAGS_poly128_m;
  poly128_val[1] = (uint64_t)FLAGS_poly128_l;
  FingerprintPolynomial<127> poly128(poly128_val);
  computeTables(file, poly128);

  CHECK_ERR(fprintf(file,
      "}  // namespace detail\n"
      "}  // namespace folly\n"));
  CHECK_ERR(fclose(file));

  return 0;
}
