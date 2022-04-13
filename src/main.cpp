/*
Copyright 2022, Yves Gallot

genefer22 is free source code, under the MIT license (see LICENSE). You can redistribute, use and/or modify it.
Please give feedback to the authors if improvement is realized. It is distributed in the hope that it will be useful.
*/

#include <cstdint>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <chrono>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include <omp.h>

#include "fp16_80.h"
#include "integer.h"

class ComplexITransform
{
private:
	const size_t _size;
	const uint32_t _b;

private:
	void unbalance(int64_t * const zi) const
	{
		const size_t size = _size;
		const int64_t base = int64_t(_b);

		int64_t f = 0;
		for (size_t i = 0; i != size; ++i)
		{
			f += zi[i];
			int64_t r = f % base;
			if (r < 0) r += base;
			zi[i] = r;
			f -= r;
			f /= base;
		}

		while (f != 0)
		{
			f = -f;		// a[n] = -a[0]

			for (size_t i = 0; i != size; ++i)
			{
				f += zi[i];
				int64_t r = f % base;
				if (r < 0) r += base;
				zi[i] = r;
				f -= r;
				f /= base;
				if (f == 0) break;
			}

			if (f == 1)
			{
				bool isMinusOne = true;
				for (size_t i = 0; i != size; ++i)
				{
					if (zi[i] != 0)
					{
						isMinusOne = false;
						break;
					}
				}
				if (isMinusOne)
				{
					// -1 cannot be unbalanced
					zi[0] = -1;
					break;
				}
			}
		}
	}

public:
	virtual double squareDup(const bool dup) = 0;
	virtual void getZi(int64_t * const zi) const = 0;

public:
	ComplexITransform(const size_t size, const uint32_t b) : _size(size), _b(b) {}
	virtual ~ComplexITransform() {}

	bool isOne(uint64_t & residue) const
	{
		const size_t size = _size;

		int64_t * const zi = new int64_t[size];
		getZi(zi);

		unbalance(zi);

		bool isOne = (zi[0] == 1);
		if (isOne) for (size_t k = 1; k < size; ++k) isOne &= (zi[k] == 0);

		uint64_t res = 0;
		for (size_t i = 8; i != 0; --i) res = (res << 8) | (unsigned char)zi[size - i];
		residue = res;

		delete[] zi;

		return isOne;
	}	
};

struct Complex
{
	double real, imag;

	explicit Complex() {}
	constexpr explicit Complex(const double re, const double im) : real(re), imag(im) {}

	static Complex exp2iPi(const size_t a, const size_t b)
	{
#define	C2PI	6.2831853071795864769252867665590057684L
		const long double alpha = C2PI * (long double)a / (long double)b;
		const double cs = (double)cosl(alpha), sn = (double)sinl(alpha);
		return Complex(cs, sn / cs);
	}
};

template<size_t N>
class Vd
{
private:
	double __attribute__((aligned(sizeof(double) * N))) r[N];

public:
	explicit Vd() {}
	constexpr explicit Vd(const double & f) { r[0] = f; for (size_t i = 1; i < N; ++i) r[i] = 0; }
	constexpr Vd(const Vd & rhs) { for (size_t i = 0; i < N; ++i) r[i] = rhs.r[i]; }
	Vd & operator=(const Vd & rhs) { for (size_t i = 0; i < N; ++i) r[i] = rhs.r[i]; return *this; }

	static Vd read(const double * const mem)
	{
		Vd vd;
		for (size_t i = 0; i < N; ++i) vd.r[i] = mem[i];
		return vd;
	}

	void write(double * const mem) const
	{
		for (size_t i = 0; i < N; ++i) mem[i] = r[i];
	}

	// static Vd read(const double * const mem1, const double * const mem2)
	// {
	// 	Vd vd;
	// 	for (size_t i = 0; i < N / 2; ++i) vd.r[i + 0 * N / 2] = mem1[i];
	// 	for (size_t i = 0; i < N / 2; ++i) vd.r[i + 1 * N / 2] = mem2[i];
	// 	return vd;
	// }

	// void write(double * const mem1, double * const mem2) const
	// {
	// 	for (size_t i = 0; i < N / 2; ++i) mem1[i] = r[i + 0 * N / 2];
	// 	for (size_t i = 0; i < N / 2; ++i) mem2[i] = r[i + 1 * N / 2];
	// }

	static Vd broadcast(const double * const mem) { Vd vd; const double f = mem[0]; for (size_t i = 0; i < N; ++i) vd.r[i] = f; return vd; }
	static Vd broadcast(const double * const mem_l, const double * const mem_h)
	{
		Vd vd;
		const double f_l = mem_l[0];
		for (size_t i = 0; i < N / 2; ++i) vd.r[i] = f_l;
		const double f_h = mem_h[0];
		for (size_t i = 0; i < N / 2; ++i) vd.r[i + N / 2] = f_h;
		return vd;
	}

	double operator[](const size_t i) const { return r[i]; }
	void set(const size_t i, const double f) { r[i] = f; }

	bool isZero() const { bool zero = true; for (size_t i = 0; i < N; ++i) zero &= (r[i] == 0.0); return zero; }

	// Vd operator-() const { Vd vd; for (size_t i = 0; i < N; ++i) vd.r[i] = -r[i]; return vd; }

	Vd & operator+=(const Vd & rhs) { for (size_t i = 0; i < N; ++i) r[i] += rhs.r[i]; return *this; }
	Vd & operator-=(const Vd & rhs) { for (size_t i = 0; i < N; ++i) r[i] -= rhs.r[i]; return *this; }
	Vd & operator*=(const Vd & rhs) { for (size_t i = 0; i < N; ++i) r[i] *= rhs.r[i]; return *this; }
	Vd & operator*=(const double & f) { for (size_t i = 0; i < N; ++i) r[i] *= f; return *this; }

	Vd operator+(const Vd & rhs) const { Vd vd = *this; vd += rhs; return vd; }
	Vd operator-(const Vd & rhs) const { Vd vd = *this; vd -= rhs; return vd; }
	Vd operator*(const Vd & rhs) const { Vd vd = *this; vd *= rhs; return vd; }
	Vd operator*(const double & f) const { Vd vd = *this; vd *= f; return vd; }

	Vd abs() const { Vd vd; for (size_t i = 0; i < N; ++i) vd.r[i] = std::fabs(r[i]); return vd; }
	Vd round() const { Vd vd; for (size_t i = 0; i < N; ++i) vd.r[i] = std::round(r[i]); return vd; }

	Vd & max(const Vd & rhs) { for (size_t i = 0; i < N; ++i) r[i] = std::max(r[i], rhs.r[i]); return *this; }

	double max() const { double m = r[0]; for (size_t i = 1; i < N; ++i) m = std::max(m, r[i]); return m; }
};

#define COMPACT	true

template<size_t N>
class Vcx
{
private:
	Vd<N> re, im;

public:
	explicit Vcx() {}
	constexpr explicit Vcx(const double & real) : re(real), im(0.0) {}
	constexpr Vcx(const Vcx & rhs) : re(rhs.re), im(rhs.im) {}
	constexpr explicit Vcx(const Vd<N> & real, const Vd<N> & imag) : re(real), im(imag) {}
	Vcx & operator=(const Vcx & rhs) { re = rhs.re; im = rhs.im; return *this; }

	static Vcx broadcast(const Complex * const mem) { return Vcx(Vd<N>::broadcast(&mem->real), Vd<N>::broadcast(&mem->imag)); }
	static Vcx broadcast(const Complex * const mem_l, const Complex * const mem_h)
	{
		return Vcx(Vd<N>::broadcast(&mem_l->real, &mem_h->real), Vd<N>::broadcast(&mem_l->imag, &mem_h->imag));
	}

	static Vcx read(const Complex * const mem)
	{
		Vcx vcx;
#ifdef COMPACT
		const double * const dmem = (const double *)(mem);
		vcx.re = Vd<N>::read(&dmem[0]);
		vcx.im = Vd<N>::read(&dmem[N]);
#else
		for (size_t i = 0; i < N; ++i)
		{
			vcx.re.set(i, mem[i].real);
			vcx.im.set(i, mem[i].imag);
		}
#endif
		return vcx;
	}
	void write(Complex * const mem) const
	{
#ifdef COMPACT
		double * const dmem = (double *)(mem);
		re.write(&dmem[0]);
		im.write(&dmem[N]);
#else
		for (size_t i = 0; i < N; ++i) mem[i] = Complex(re[i], im[i]);
#endif
	}

// 	static Vcx read(const Complex * const mem1, const Complex * const mem2)
// 	{
// 		Vcx vcx;
// #ifdef COMPACT
// 		const double * const dmem1 = (const double *)(mem1);
// 		const double * const dmem2 = (const double *)(mem2);
// 		vcx.re = Vd<N>::read(&dmem1[0 * N / 2], &dmem2[0 * N / 2]);
// 		vcx.im = Vd<N>::read(&dmem1[1 * N / 2], &dmem2[1 * N / 2]);
// #else
// 		for (size_t i = 0; i < N / 2; ++i)
// 		{
// 			vcx.re.set(i, mem1[i].real);
// 			vcx.im.set(i, mem1[i].imag);
// 		}
// 		for (size_t i = 0; i < N / 2; ++i)
// 		{
// 			vcx.re.set(i + N / 2, mem2[i].real);
// 			vcx.im.set(i + N / 2, mem2[i].imag);
// 		}
// #endif
// 		return vcx;
// 	}
// 	void write(Complex * const mem1, Complex * const mem2) const
// 	{
// #ifdef COMPACT
// 		double * const dmem1 = (double *)(mem1);
// 		double * const dmem2 = (double *)(mem2);
// 		re.write(&dmem1[0 * N / 2], &dmem2[0 * N / 2]);
// 		im.write(&dmem1[1 * N / 2], &dmem2[1 * N / 2]);
// #else
// 		for (size_t i = 0; i < N / 2; ++i) mem1[i] = Complex(re[i], im[i]);
// 		for (size_t i = 0; i < N / 2; ++i) mem2[i] = Complex(re[i + N / 2], im[i + N / 2]);
// #endif
// 	}

	static void swap(Vcx & vc1, Vcx & vc2)
	{
		Vcx nvc1, nvc2;
		for (size_t i = 0; i < N / 2; ++i)
		{
			nvc1.re.set(i + 0 * N / 2, vc1[i].real);
			nvc1.re.set(i + 1 * N / 2, vc2[i].real);
			nvc2.re.set(i + 0 * N / 2, vc1[i + N / 2].real);
			nvc2.re.set(i + 1 * N / 2, vc2[i + N / 2].real);

			nvc1.im.set(i + 0 * N / 2, vc1[i].imag);
			nvc1.im.set(i + 1 * N / 2, vc2[i].imag);
			nvc2.im.set(i + 0 * N / 2, vc1[i + N / 2].imag);
			nvc2.im.set(i + 1 * N / 2, vc2[i + N / 2].imag);
		}

		vc1 = nvc1; vc2 = nvc2;
	}

	Complex operator[](const size_t i) const { return Complex(re[i], im[i]); }
	void set(const size_t i, const Complex & z) { re.set(i, z.real); im.set(i, z.imag); }

	bool isZero() const { return (re.isZero() & im.isZero()); }

	Vcx & operator+=(const Vcx & rhs) { re += rhs.re; im += rhs.im; return *this; }
	Vcx & operator-=(const Vcx & rhs) { re -= rhs.re; im -= rhs.im; return *this; }
	Vcx & operator*=(const double & f) { re *= f; im *= f; return *this; }

	Vcx operator+(const Vcx & rhs) const { return Vcx(re + rhs.re, im + rhs.im); }
	Vcx operator-(const Vcx & rhs) const { return Vcx(re - rhs.re, im - rhs.im); }
	Vcx addi(const Vcx & rhs) const { return Vcx(re - rhs.im, im + rhs.re); }
	Vcx subi(const Vcx & rhs) const { return Vcx(re + rhs.im, im - rhs.re); }
	Vcx sub_i(const Vcx & rhs) const { return Vcx(rhs.im - im, re - rhs.re); }

	Vcx operator*(const Vcx & rhs) const { return Vcx(re * rhs.re - im * rhs.im, im * rhs.re + re * rhs.im); }
	Vcx operator*(const double & f) const { return Vcx(re * f, im * f); }
	Vcx mul1i() const { return Vcx(re - im, im + re); }
	Vcx mul1mi() const { return Vcx(re + im, im - re); }
	// Vcx muli() const { return Vcx(-im, re); }
	// Vcx mulmi() const { return Vcx(im, -re); }

	Vcx sqr() const { return Vcx(re * re - im * im, (re + re) * im); }

	Vcx mulW(const Vcx & rhs) const { return Vcx((re - im * rhs.im) * rhs.re, (im + re * rhs.im) * rhs.re); }
	Vcx mulWconj(const Vcx & rhs) const { return Vcx((re + im * rhs.im) * rhs.re, (im - re * rhs.im) * rhs.re); }

	Vcx abs() const { return Vcx(re.abs(), im.abs()); }
	Vcx round() const { return Vcx(re.round(), im.round()); }

	Vcx & max(const Vcx & rhs) { re.max(rhs.re); im.max(rhs.im); return *this; }

	double max() const { return std::max(re.max(), im.max()); }

	void shift(const Vcx & rhs, const bool rotate)
	{
		// f x^n = -f
		for (size_t i = N - 1; i > 0; --i)
		{
			re.set(i, re[i - 1]);
			im.set(i, im[i - 1]);
		}
		re.set(0, rotate ? -rhs.im[N - 1] : rhs.re[N - 1]);
		im.set(0, rotate ?  rhs.re[N - 1] : rhs.im[N - 1]);
	}
};

static constexpr double csqrt2_2 = 0.707106781186547524400844362104849039284835937688;
static constexpr Complex cs2pi_1_16 = Complex(0.92387953251128675612818318939678828682, 0.41421356237309504880168872420969807857);
static constexpr Complex cs2pi_1_32 = Complex(0.98078528040323044912618223613423903697, 0.19891236737965800691159762264467622860);
static constexpr Complex cs2pi_5_32 = Complex(0.55557023301960222474283081394853287438, 1.49660576266548901760113513494247691870);

template<size_t N>
class Vradix4
{
private:
	using Vc = Vcx<N>;
	Vc z[4];

public:
	explicit Vradix4(const Complex * const mem, const size_t step)
	{
		for (size_t i = 0; i < 4; ++i) z[i] = Vc::read(&mem[i * step]);
	}

	void store(Complex * const mem, const size_t step) const
	{
		for (size_t i = 0; i < 4; ++i) z[i].write(&mem[i * step]);
	}

	// explicit Vradix4(const Complex * const mem1, const Complex * const mem2, const size_t step)
	// {
	// 	for (size_t i = 0; i < 4; ++i) z[i] = Vc::read(&mem1[i * step], &mem2[i * step]);
	// }

	// void store(Complex * const mem1, Complex * const mem2, const size_t step) const
	// {
	// 	for (size_t i = 0; i < 4; ++i) z[i].write(&mem1[i * step], &mem2[i * step]);
	// }

	explicit Vradix4(const Vc * const mem, const size_t step)
	{
		for (size_t i = 0; i < 4; ++i) z[i] = mem[i * step];
	}

	void store(Vc * const mem, const size_t step) const
	{
		for (size_t i = 0; i < 4; ++i) mem[i * step] = z[i];
	}


	void forward4e(const Vc & w0, const Vc & w1)
	{
		const Vc u0 = z[0], u2 = z[2].mulW(w0), u1 = z[1], u3 = z[3].mulW(w0);
		const Vc v0 = u0 + u2, v2 = u0 - u2, v1 = Vc(u1 + u3).mulW(w1), v3 = Vc(u1 - u3).mulW(w1);
		z[0] = v0 + v1; z[1] = v0 - v1; z[2] = v2.addi(v3); z[3] = v2.subi(v3);
	}

	void forward4o(const Vc & w0, const Vc & w2)
	{
		const Vc u0 = z[0], u2 = z[2].mulW(w0), u1 = z[1], u3 = z[3].mulW(w0);
		const Vc v0 = u0.addi(u2), v2 = u0.subi(u2), v1 = u1.addi(u3).mulW(w2), v3 = u1.subi(u3).mulW(w2);
		z[0] = v0 + v1; z[1] = v0 - v1; z[2] = v2.addi(v3); z[3] = v2.subi(v3);
	}

	void backward4e(const Vc & w0, const Vc & w1)
	{
		const Vc v0 = z[0], v1 = z[1], v2 = z[2], v3 = z[3];
		const Vc u0 = v0 + v1, u1 = Vc(v0 - v1).mulWconj(w1), u2 = v2 + v3, u3 = Vc(v2 - v3).mulWconj(w1);
		z[0] = u0 + u2; z[2] = Vc(u0 - u2).mulWconj(w0); z[1] = u1.subi(u3); z[3] = u1.addi(u3).mulWconj(w0);
	}

	void backward4o(const Vc & w0, const Vc & w2)
	{
		const Vc v0 = z[0], v1 = z[1], v2 = z[2], v3 = z[3];
		const Vc u0 = v0 + v1, u1 = Vc(v0 - v1).mulWconj(w2), u2 = v2 + v3, u3 = Vc(v2 - v3).mulWconj(w2);
		z[0] = u0 + u2; z[2] = u2.sub_i(u0).mulWconj(w0); z[1] = u1.subi(u3); z[3] = u3.subi(u1).mulWconj(w0);
	}

	void forward4_0(const Vc & w0)
	{
		const Vc u0 = z[0], u2 = z[2].mul1i(), u1 = z[1].mulW(w0), u3 = z[3].mulWconj(w0);
		const Vc v0 = u0 + u2 * csqrt2_2, v2 = u0 - u2 * csqrt2_2, v1 = u1.addi(u3), v3 = u3.addi(u1);
		z[0] = v0 + v1; z[1] = v0 - v1; z[2] = v2 + v3; z[3] = v2 - v3;
	}

	void backward4_0(const Vc & w0)
	{
		const Vc v0 = z[0], v1 = z[1], v2 = z[2], v3 = z[3];
		const Vc u0 = v0 + v1, u1 = v0 - v1, u2 = v2 + v3, u3 = v2 - v3;
		z[0] = u0 + u2; z[2] = Vc(u0 - u2).mul1mi() * csqrt2_2; z[1] = u1.subi(u3).mulWconj(w0); z[3] = u3.subi(u1).mulW(w0);
	}

	void square4e(const Vc & w)
	{
		const Vc u0 = z[0], u2 = z[2].mulW(w), u1 = z[1], u3 = z[3].mulW(w);
		const Vc v0 = u0 + u2, v2 = u0 - u2, v1 = u1 + u3, v3 = u1 - u3;
		const Vc s0 = v0.sqr() + v1.sqr().mulW(w), s1 = (v0 + v0) * v1, s2 = v2.sqr() - v3.sqr().mulW(w), s3 = (v2 + v2) * v3;
		z[0] = s0 + s2; z[2] = Vc(s0 - s2).mulWconj(w); z[1] = s1 + s3; z[3] = Vc(s1 - s3).mulWconj(w);
	}

	void square4o(const Vc & w)
	{
		const Vc u0 = z[0], u2 = z[2].mulW(w), u1 = z[1], u3 = z[3].mulW(w);
		const Vc v0 = u0.addi(u2), v2 = u0.subi(u2), v1 = u1.addi(u3), v3 = u3.addi(u1);
		const Vc s0 = v1.sqr().mulW(w).subi(v0.sqr()), s1 = (v0 + v0) * v1, s2 = v2.sqr().addi(v3.sqr().mulW(w)), s3 = (v2 + v2) * v3;
		z[0] = s2.addi(s0); z[2] = s0.addi(s2).mulWconj(w); z[1] = s1.subi(s3); z[3] = s3.subi(s1).mulWconj(w);
	}
};

template<size_t N>
class Vradix8
{
private:
	using Vc = Vcx<N>;
	Vc z[8];

public:
	explicit Vradix8(const Complex * const mem, const size_t step)
	{
		for (size_t i = 0; i < 8; ++i) z[i] = Vc::read(&mem[i * step]);
	}

	void store(Complex * const mem, const size_t step) const
	{
		for (size_t i = 0; i < 8; ++i) z[i].write(&mem[i * step]);
	}

	void forward8_0()
	{
		const Vc w0 = Vc::broadcast(&cs2pi_1_16);
		const Vc u0 = z[0], u4 = z[4].mul1i(), u2 = z[2].mulW(w0), u6 = z[6].mul1i().mulW(w0);
		const Vc u1 = z[1], u5 = z[5].mul1i(), u3 = z[3].mulW(w0), u7 = z[7].mul1i().mulW(w0);
		const Vc v0 = u0 + u4 * csqrt2_2, v4 = u0 - u4 * csqrt2_2, v2 = u2 + u6 * csqrt2_2, v6 = u2 - u6 * csqrt2_2;
		const Vc w1 = Vc::broadcast(&cs2pi_1_32), w2 = Vc::broadcast(&cs2pi_5_32);;
		const Vc v1 = Vc(u1 + u5 * csqrt2_2).mulW(w1), v5 = Vc(u1 - u5 * csqrt2_2).mulW(w2);
		const Vc v3 = Vc(u3 + u7 * csqrt2_2).mulW(w1), v7 = Vc(u3 - u7 * csqrt2_2).mulW(w2);
		const Vc s0 = v0 + v2, s2 = v0 - v2, s1 = v1 + v3, s3 = v1 - v3;
		const Vc s4 = v4.addi(v6), s6 = v4.subi(v6), s5 = v5.addi(v7), s7 = v5.subi(v7);
		z[0] = s0 + s1; z[1] = s0 - s1; z[2] = s2.addi(s3); z[3] = s2.subi(s3);
		z[4] = s4 + s5; z[5] = s4 - s5; z[6] = s6.addi(s7); z[7] = s6.subi(s7);
	}

	void backward8_0()
	{
		const Vc s0 = z[0], s1 = z[1], s2 = z[2], s3 = z[3], s4 = z[4], s5 = z[5], s6 = z[6], s7 = z[7];
		const Vc w1 = Vc::broadcast(&cs2pi_1_32), w2 = Vc::broadcast(&cs2pi_5_32);;
		const Vc v0 = s0 + s1, v1 = Vc(s0 - s1).mulWconj(w1), v2 = s2 + s3, v3 = Vc(s2 - s3).mulWconj(w1);
		const Vc v4 = s4 + s5, v5 = Vc(s4 - s5).mulWconj(w2), v6 = s6 + s7, v7 = Vc(s6 - s7).mulWconj(w2);
		const Vc u0 = v0 + v2, u2 = v0 - v2, u4 = v4 + v6, u6 = v4 - v6;
		const Vc u1 = v1.subi(v3), u3 = v1.addi(v3), u5 = v5.subi(v7), u7 = v5.addi(v7);
		const Vc w0 = Vc::broadcast(&cs2pi_1_16);
		z[0] = u0 + u4; z[4] = Vc(u0 - u4).mul1mi() * csqrt2_2; z[2] = u2.subi(u6).mulWconj(w0); z[6] = u6.subi(u2).mulW(w0);
		z[1] = u1 + u5; z[5] = Vc(u1 - u5).mul1mi() * csqrt2_2; z[3] = u3.subi(u7).mulWconj(w0); z[7] = u7.subi(u3).mulW(w0);
	}
};

template<size_t N>
class Vcx8
{
private:
	using Vc = Vcx<N>;
	Vc z[8];

private:
	Vcx8() {}

public:
	explicit Vcx8(const Complex * const mem)
	{
		for (size_t i = 0; i < 8; ++i) z[i] = Vc::read(&mem[i * N]);
	}

	void store(Complex * const mem) const
	{
		for (size_t i = 0; i < 8; ++i) z[i].write(&mem[i * N]);
	}

	explicit Vcx8(const Complex * const mem, const size_t step)
	{
		for (size_t i = 0; i < 8; ++i)
		{
			const size_t i_h = (N * i) / 8, i_l = (N * i) % 8;
			z[i] = Vc::read(&mem[step * i_h + i_l]);
		}
	}

	void store(Complex * const mem, const size_t step) const
	{
		for (size_t i = 0; i < 8; ++i)
		{
			const size_t i_h = (N * i) / 8, i_l = (N * i) % 8;
			z[i].write(&mem[step * i_h + i_l]);
		}
	}

	Vc * getZ() { return z; }

	Vcx8 transpose_in() const
	{
		Vcx8 vc8;
		for (size_t i = 0; i < N; ++i)
		{
			for (size_t j = 0; j < 8; ++j)
			{
				const size_t ind = 8 * i + j;
				vc8.z[j].set(i, z[ind / N][ind % N]);
			}
		}
		return vc8;
	}

	Vcx8 transpose_out() const
	{
		Vcx8 vc8;
		for (size_t i = 0; i < N; ++i)
		{
			for (size_t j = 0; j < 8; ++j)
			{
				const size_t ind = 8 * i + j;
				vc8.z[ind / N].set(ind % N, z[j][i]);
			}
		}
		return vc8;
	}
};

template<size_t N, size_t VSIZE>
class CZIT_CPU_vec_mt : public ComplexITransform
{
private:
	static const size_t n_io = 16 * 4;		// power of 4, n_io >= 64, n >= 16 * n_io, n >= num_threads * n_io
	static const size_t n_io_s = n_io / 4 / 2;
	static const size_t n_io_inv = N / n_io / VSIZE;

	using Vc = Vcx<VSIZE>;
	using Vr4 = Vradix4<VSIZE>;
	using Vr8 = Vradix8<VSIZE>;
	using Vc8 = Vcx8<VSIZE>;

	const fp16_80 sqrt_b;

	const uint32_t _b;
	const size_t _num_threads;
	const double _sb, _isb, _fsb;
	Complex * const _w122i;
	Vc * const _ws;
	Complex * const _z;
	Vc * const _f;

private:
	static size_t bitRev(const size_t i, const size_t n)
	{
		size_t r = 0;
		for (size_t k = n, j = i; k > 1; k /= 2, j /= 2) r = (2 * r) | (j % 2);
		return r;
	}

	static constexpr size_t index(const size_t k) { const size_t j = k / n_io, i = k % n_io; return j * (n_io + 64 / sizeof(Complex)) + i; }

	template <size_t stepi, size_t count>
	static void forward4e(const size_t mi, Complex * const z, const Vc & w0, const Vc & w1)
	{
		for (size_t j = 0; j < mi; j += (stepi == 1) ? VSIZE : stepi)
		{
			for (size_t i = 0; i < count; i += (count != 1) ? VSIZE : 1)
			{
				Complex * const zi = &z[j + i];
				Vr4 vr(zi, mi);
				vr.forward4e(w0, w1);
				vr.store(zi, mi);
			}
		}
	}

	template <size_t stepi, size_t count>
	static void forward4o(const size_t mi, Complex * const z, const Vc & w0, const Vc & w2)
	{
		for (size_t j = 0; j < mi; j += (stepi == 1) ? VSIZE : stepi)
		{
			for (size_t i = 0; i < count; i += (count != 1) ? VSIZE : 1)
			{
				Complex * const zi = &z[j + i];
				Vr4 vr(zi, mi);
				vr.forward4o(w0, w2);
				vr.store(zi, mi);
			}
		}
	}

	template <size_t stepi, size_t count>
	static void backward4e(const size_t mi, Complex * const z, const Vc & w0, const Vc & w1)
	{
		for (size_t j = 0; j < mi; j += (stepi == 1) ? VSIZE : stepi)
		{
			for (size_t i = 0; i < count; i += (count != 1) ? VSIZE : 1)
			{
				Complex * const zi = &z[j + i];
				Vr4 vr(zi, mi);
				vr.backward4e(w0, w1);
				vr.store(zi, mi);
			}
		}
	}

	template <size_t stepi, size_t count>
	static void backward4o(const size_t mi, Complex * const z, const Vc & w0, const Vc & w2)
	{
		for (size_t j = 0; j < mi; j += (stepi == 1) ? VSIZE : stepi)
		{
			for (size_t i = 0; i < count; i += (count != 1) ? VSIZE : 1)
			{
				Complex * const zi = &z[j + i];
				Vr4 vr(zi, mi);
				vr.backward4o(w0, w2);
				vr.store(zi, mi);
			}
		}
	}

	static void forward4e_4(Complex * const z1, Complex * const z2, const Vc & w0, const Vc & w1)
	{
		Vr4 vr(z1, z2, 4);
		vr.forward4e(w0, w1);
		vr.store(z1, z2, 4);
	}

	static void forward4o_4(Complex * const z1, Complex * const z2, const Vc & w0, const Vc & w2)
	{
		Vr4 vr(z1, z2, 4);
		vr.forward4o(w0, w2);
		vr.store(z1, z2, 4);
	}

	static void _forward4e_4(Vc * const z, const Vc & w0, const Vc & w1)
	{
		Vr4 vr(z, 1);
		vr.forward4e(w0, w1);
		vr.store(z, 1);
	}

	static void _forward4o_4(Vc * const z, const Vc & w0, const Vc & w2)
	{
		Vr4 vr(z, 1);
		vr.forward4o(w0, w2);
		vr.store(z, 1);
	}

	static void backward4e_4(Vc * const z, const Vc & w0, const Vc & w1)
	{
		Vr4 vr(z, 1);
		vr.backward4e(w0, w1);
		vr.store(z, 1);
	}

	static void backward4o_4(Vc * const z, const Vc & w0, const Vc & w2)
	{
		Vr4 vr(z, 1);
		vr.backward4o(w0, w2);
		vr.store(z, 1);
	}

	template <size_t stepi, size_t count>
	static void forward4_0(const size_t mi, Complex * const z)
	{
		const Vc w0 = Vc::broadcast(&cs2pi_1_16);
		for (size_t j = 0; j < mi; j += stepi)
		{
			for (size_t i = 0; i < count; i += VSIZE)
			{
				Complex * const zi = &z[j + i];
				Vr4 vr(zi, mi);
				vr.forward4_0(w0);
				vr.store(zi, mi);
			}
		}
	}

	template <size_t stepi, size_t count>
	static void backward4_0(const size_t mi, Complex * const z)
	{
		const Vc w0 = Vc::broadcast(&cs2pi_1_16);
		for (size_t j = 0; j < mi; j += stepi)
		{
			for (size_t i = 0; i < count; i += VSIZE)
			{
				Complex * const zi = &z[j + i];
				Vr4 vr(zi, mi);
				vr.backward4_0(w0);
				vr.store(zi, mi);
			}
		}
	}

	template <size_t stepi, size_t count>
	static void forward8_0(const size_t mi, Complex * const z)
	{
		for (size_t j = 0; j < mi; j += stepi)
		{
			for (size_t i = 0; i < count; i += VSIZE)
			{
				Complex * const zi = &z[j + i];
				Vr8 vr(zi, mi);
				vr.forward8_0();
				vr.store(zi, mi);
			}
		}
	}

	template <size_t stepi, size_t count>
	static void backward8_0(const size_t mi, Complex * const z)
	{
		for (size_t j = 0; j < mi; j += stepi)
		{
			for (size_t i = 0; i < count; i += VSIZE)
			{
				Complex * const zi = &z[j + i];
				Vr8 vr(zi, mi);
				vr.backward8_0();
				vr.store(zi, mi);
			}
		}
	}

	static void square4e(Vc * const z, const Vc & w)
	{
		Vr4 vr(z, 1);
		vr.square4e(w);
		vr.store(z, 1);
	}

	static void square4o(Vc * const z, const Vc & w)
	{
		Vr4 vr(z, 1);
		vr.square4o(w);
		vr.store(z, 1);
	}

	static void forward_out(Complex * const z, const size_t lh, const Complex * const w122i)
	{
		size_t s = (N / 4) / n_io / 2; for (; s >= 4 * 2; s /= 4);

		if (s == 4) forward8_0<index(n_io), 2 * 4>(index(N / 8), &z[2 * 4 * lh]);
		else        forward4_0<index(n_io), 2 * 4>(index(N / 4), &z[2 * 4 * lh]);

		for (size_t mi = index((s == 4) ? N / 32 : N / 16); mi >= index(n_io); mi /= 4, s *= 4)
		{
			for (size_t j = 0; j < s; ++j)
			{
				const size_t k = 2 * 4 * lh + 8 * mi * j;
				const Complex * const w = &w122i[s + 3 * j];
				const Vc w0 = Vc::broadcast(&w[0]), w1 = Vc::broadcast(&w[1]);
				forward4e<index(n_io), 2 * 4>(mi, &z[k + 0 * 4 * mi], w0, w1);
				const Vc w2 = Vc::broadcast(&w[2]);
				forward4o<index(n_io), 2 * 4>(mi, &z[k + 1 * 4 * mi], w0, w2);
			}
		}
	}

	static void backward_out(Complex * const z, const size_t lh, const Complex * const w122i)
	{
		size_t s = (N / 4) / n_io / 2;
		for (size_t mi = index(n_io); s >= 2; mi *= 4, s /= 4)
		{
			for (size_t j = 0; j < s; ++j)
			{
				const size_t k = 2 * 4 * lh + 8 * mi * j;
				const Complex * const w = &w122i[s + 3 * j];
				const Vc w0 = Vc::broadcast(&w[0]), w1 = Vc::broadcast(&w[1]);
				backward4e<index(n_io), 2 * 4>(mi, &z[k + 0 * 4 * mi], w0, w1);
				const Vc w2 = Vc::broadcast(&w[2]);
				backward4o<index(n_io), 2 * 4>(mi, &z[k + 1 * 4 * mi], w0, w2);
			}
		}

		if (s == 1) backward8_0<index(n_io), 2 * 4>(index(N / 8), &z[2 * 4 * lh]);
		else        backward4_0<index(n_io), 2 * 4>(index(N / 4), &z[2 * 4 * lh]);
	}

	void pass1(const size_t thread_id)
	{
		const Complex * const w122i = _w122i;
		const Vc * const ws = _ws;
		Complex * const z = _z;

		const size_t num_threads = _num_threads, s_io = N / n_io;
		const size_t l_min = thread_id * s_io / num_threads, l_max = (thread_id + 1 == num_threads) ? s_io : (thread_id + 1) * s_io / num_threads;
		for (size_t l = l_min; l < l_max; ++l)
		{
			Complex * const zl = &z[index(n_io * l)];

			// forward_in
			{
				const Complex * const w = &w122i[s_io / 2 + 3 * (l / 2)];
				const Vc w0 = Vc::broadcast(&w[0]);

				if (l % 2 == 0) { const Vc w1 = Vc::broadcast(&w[1]); forward4e<1, 1>(n_io / 4, zl, w0, w1); }
				else            { const Vc w2 = Vc::broadcast(&w[2]); forward4o<1, 1>(n_io / 4, zl, w0, w2); }
			}

			for (size_t m = n_io / 16, s = 2; m >= ((VSIZE > 4) ? 16 : 4); m /= 4, s *= 4)
			{
				for (size_t j = 0; j < s; ++j)
				{
					Complex * const zj = &zl[8 * m * j];
					const Complex * const w = &w122i[(s_io + 3 * l) * s + 3 * j];
					const Vc w0 = Vc::broadcast(&w[0]), w1 = Vc::broadcast(&w[1]);
					forward4e<1, 1>(m, &zj[0 * 4 * m], w0, w1);
					const Vc w2 = Vc::broadcast(&w[2]);
					forward4o<1, 1>(m, &zj[1 * 4 * m], w0, w2);
				}
			}

			if (VSIZE > 4)
			{
				for (size_t j = 0; j < n_io / 32; j += 2)
				{
					Complex * const zj = &zl[32 * j];
					const Complex * const w = &w122i[(s_io + 3 * l) * (n_io / 32) + 3 * j];
					const Vc w0 = Vc::broadcast(&w[0], &w[3]), w1 = Vc::broadcast(&w[1], &w[4]);

					// forward4e_4(&zj[0 * 16], &zj[2 * 16], w0, w1);
					// const Vc w2 = Vc::broadcast(&w[2], &w[5]);
					// forward4o_4(&zj[1 * 16], &zj[3 * 16], w0, w2);

					Vc t[8];
					for (size_t i = 0; i < 8; ++i) t[i] = Vc::read(&zj[i * VSIZE]);

					for (size_t i = 0; i < 4; ++i) Vc::swap(t[i], t[i + 4]);

					// t0, t4, t1, t5, ...

					Vc nt[8];
					for (size_t i = 0; i < 4; ++i) { nt[2 * i + 0] = t[i]; nt[2 * i + 1] = t[i + 4]; }

					_forward4e_4(&nt[0], w0, w1);
					const Vc w2 = Vc::broadcast(&w[2], &w[5]);
					_forward4o_4(&nt[4], w0, w2);

					for (size_t i = 0; i < 4; ++i) { t[i] = nt[2 * i + 0]; t[i + 4] = nt[2 * i + 1]; }

					for (size_t i = 0; i < 4; ++i) Vc::swap(t[i], t[i + 4]);

					for (size_t i = 0; i < 8; ++i) t[i].write(&zj[i * VSIZE]);
				}
			}

			// square
			for (size_t j = 0; j < n_io / 8 / VSIZE; ++j)
			{
				Complex * const zj = &zl[8 * j * VSIZE];

				Vc8 z8(zj);
				z8 = z8.transpose_in();

				const Vc & ws0 = ws[l * n_io / 8 / VSIZE + j];
				Vc * const zt = z8.getZ();
				square4e(&zt[0], ws0);	// => Vc8 ??
				square4o(&zt[4], ws0);

				z8 = z8.transpose_out();
				z8.store(zj);
			}

			if (VSIZE > 4)
			{
				for (size_t j = 0; j < n_io / 32; j += 2)
				{
					Complex * const zj = &zl[32 * j];
					const Complex * const w = &w122i[(s_io + 3 * l) * (n_io / 32) + 3 * j];
					const Vc w0 = Vc::broadcast(&w[0], &w[3]), w1 = Vc::broadcast(&w[1], &w[4]);

					// backward4e_4(&zj[0 * 16], &zj[2 * 16], w0, w1);
					// const Vc w2 = Vc::broadcast(&w[2], &w[5]);
					// backward4o_4(&zj[1 * 16], &zj[3 * 16], w0, w2);

					Vc t[8];
					for (size_t i = 0; i < 8; ++i) t[i] = Vc::read(&zj[i * VSIZE]);

					for (size_t i = 0; i < 4; ++i) Vc::swap(t[i], t[i + 4]);

					Vc nt[8];
					for (size_t i = 0; i < 4; ++i) { nt[2 * i + 0] = t[i]; nt[2 * i + 1] = t[i + 4]; }

					backward4e_4(&nt[0], w0, w1);
					const Vc w2 = Vc::broadcast(&w[2], &w[5]);
					backward4o_4(&nt[4], w0, w2);

					for (size_t i = 0; i < 4; ++i) { t[i] = nt[2 * i + 0]; t[i + 4] = nt[2 * i + 1]; }

					for (size_t i = 0; i < 4; ++i) Vc::swap(t[i], t[i + 4]);

					for (size_t i = 0; i < 8; ++i) t[i].write(&zj[i * VSIZE]);
				}
			}

			// backward_in
			for (size_t m = ((VSIZE > 4) ? 16 : 4), s = n_io / 4 / m / 2; m <= n_io / 16; m *= 4, s /= 4)
			{
				for (size_t j = 0; j < s; ++j)
				{
					Complex * const zj = &zl[8 * m * j];
					const Complex * const w = &w122i[(s_io + 3 * l) * s + 3 * j];
					const Vc w0 = Vc::broadcast(&w[0]), w1 = Vc::broadcast(&w[1]);
					backward4e<1, 1>(m, &zj[0 * 4 * m], w0, w1);
					const Vc w2 = Vc::broadcast(&w[2]);
					backward4o<1, 1>(m, &zj[1 * 4 * m], w0, w2);
				}
			}

			{
				const Complex * const w = &w122i[s_io / 2 + 3 * (l / 2)];
				const Vc w0 = Vc::broadcast(&w[0]);

				if (l % 2 == 0) { const Vc w1 = Vc::broadcast(&w[1]); backward4e<1, 1>(n_io / 4, zl, w0, w1); }
				else            { const Vc w2 = Vc::broadcast(&w[2]); backward4o<1, 1>(n_io / 4, zl, w0, w2); }
			}
		}
	}

	double pass2(const size_t thread_id, const bool dup)
	{
		const double b = double(_b);
		const Complex * const w122i = _w122i;
		Complex * const z = _z;
		Vc * const f = _f;
		const double sb = _sb, isb = _isb, fsb = _fsb;

		const double b_inv = 1.0 / b, sb_inv = 1.0 / sb, t2_n = 2.0 / N, g = dup ? 2 : 1;

		Vc err = Vc(0.0);

		const size_t num_threads = _num_threads;
		const size_t l_min = thread_id * n_io_s / num_threads, l_max = (thread_id + 1 == num_threads) ? n_io_s : (thread_id + 1) * n_io_s / num_threads;
		for (size_t lh = l_min; lh < l_max; ++lh)
		{
			backward_out(z, lh, w122i);

			// carry_out
			for (size_t j = 0; j < n_io_inv; ++j)
			{
				Complex * const zj = &z[index(n_io) * VSIZE * j + 2 * 4 * lh];

				Vc8 z8(zj, index(n_io));
				z8 = z8.transpose_in();

				Vc f_k_f = Vc(0.0);
				Vc * const zt = z8.getZ();

				for (size_t l = 0; l < 4; ++l)
				{
					Vc & z0 = zt[2 * l + 0]; Vc & z1 = zt[2 * l + 1];

					const Vc o = (z0 + z1 * sb) * t2_n, oi = o.round(), d = Vc(o - oi).abs();
					const Vc f_i = f_k_f + oi * g;
					err.max(d);
					const Vc f_o = Vc(f_i * b_inv).round();
					const Vc r = f_i - f_o * b;
					f_k_f = f_o;
					const Vc irh = Vc(r * sb_inv).round();
					z0 = (r - irh * isb) - irh * fsb; z1 = irh;
				}

				f[lh * n_io_inv + j] = f_k_f;

				z8.store(zj, index(n_io));	// transposed
			}
		}

		return err.max();
	}

	void pass3(const size_t thread_id)
	{
		const double b = double(_b);
		const Complex * const w122i = _w122i;
		Complex * const z = _z;
		Vc * const f = _f;
		const double sb = _sb, isb = _isb, fsb = _fsb;

		const double b_inv = 1.0 / b, sb_inv = 1.0 / sb;

		const size_t num_threads = _num_threads;
		const size_t l_min = thread_id * n_io_s / num_threads, l_max = (thread_id + 1 == num_threads) ? n_io_s : (thread_id + 1) * n_io_s / num_threads;
		for (size_t lh = l_min; lh < l_max; ++lh)
		{
			// carry_in
			for (size_t j = 0; j < n_io_inv; ++j)
			{
				Complex * const zj = &z[index(n_io) * VSIZE * j + 2 * 4 * lh];

				Vc8 z8(zj, index(n_io));	// transposed

				const size_t lh_prev = ((lh != 0) ? lh : n_io_s) - 1;
				Vc f_j = f[lh_prev * n_io_inv + j];
				if (lh == 0)
				{
					const size_t j_prev = ((j == 0) ? n_io_inv : j) - 1;
					f_j.shift(f[(n_io_s - 1) * n_io_inv + j_prev], j == 0);
				}

				Vc * const zt = z8.getZ();

				for (size_t l = 0; l < 4 - 1; ++l)
				{
					Vc & z0 = zt[2 * l + 0]; Vc & z1 = zt[2 * l + 1];

					const Vc o = z0 + z1 * sb, oi = o.round();
					f_j += oi;
					const Vc f_o = Vc(f_j * b_inv).round();
					const Vc r = f_j - f_o * b;
					f_j = f_o;
					const Vc irh = Vc(r * sb_inv).round();
					z0 = (r - irh * isb) - irh * fsb; z1 = irh;

					if (f_j.isZero()) break;
				}

				if (!f_j.isZero())
				{
					Vc & z0 = zt[2 * (4 - 1) + 0]; Vc & z1 = zt[2 * (4 - 1) + 1];

					const Vc o = z0 + z1 * sb, oi = o.round();
					const Vc r = f_j + oi;
					const Vc irh = Vc(r * sb_inv).round();
					z0 = (r - irh * isb) - irh * fsb; z1 = irh;
				}

				z8 = z8.transpose_out();
				z8.store(zj, index(n_io));
			}

			forward_out(z, lh, w122i);
		}
	}

public:
	CZIT_CPU_vec_mt(const uint32_t b, const size_t num_threads) : ComplexITransform(N, b),
		sqrt_b(fp16_80::sqrt(b)), _b(b), _num_threads(num_threads), _sb(double(sqrtl(b))), _isb(sqrt_b.hi()), _fsb(sqrt_b.lo()),
		_w122i(new Complex[N / 8]), _ws(new Vc[N / 8 / VSIZE]), _z(new Complex[index(N)]), _f(new Vc[N / 4 / 2 / VSIZE])
	{
		Complex * const w122i = _w122i;
		for (size_t s = N / 16; s >= 4; s /= 4)
		{
			Complex * const w_s = &w122i[2 * s / 4];
			for (size_t j = 0; j < s / 2; ++j)
			{
				const size_t r = bitRev(j, 2 * s) + 1;
				w_s[3 * j + 0] = Complex::exp2iPi(r, 8 * s);
				w_s[3 * j + 1] = Complex::exp2iPi(r, 2 * 8 * s);
				w_s[3 * j + 2] = Complex::exp2iPi(r + 2 * s, 2 * 8 * s);
			}
		}

		Vc * const ws = _ws;
		for (size_t j = 0; j < N / 8 / VSIZE; ++j)
		{
			for (size_t i = 0; i < VSIZE; ++i)
			{
				ws[j].set(i, Complex::exp2iPi(bitRev(VSIZE * j + i, 2 * (N / 4)) + 1, 8 * (N / 4)));
			}
		}

		Complex * const z = _z;
		Vc(2.0).write(&z[0]);
		for (size_t k = VSIZE; k < index(N); k += VSIZE) Vc(0.0).write(&z[k]);

		for (size_t lh = 0; lh < n_io / 4 / 2; ++lh)
		{
			forward_out(z, lh, w122i);
		}
	}

	virtual ~CZIT_CPU_vec_mt()
	{
		delete[] _w122i;
		delete[] _ws;
		delete[] _z;
		delete[] _f;
	}

	double squareDup(const bool dup) override
	{
		const size_t num_threads = _num_threads;

		double e[num_threads];

#pragma omp parallel
		{
			const size_t thread_id = size_t(omp_get_thread_num());

			pass1(thread_id);
#pragma omp barrier
			e[thread_id] = pass2(thread_id, dup);
#pragma omp barrier
			pass3(thread_id);
		}

		double err = 0;
		for (size_t i = 0; i < num_threads; ++i) err = std::max(err, e[i]);
		return err;
	}

	void getZi(int64_t * const zi) const override
	{
		const Complex * const z = _z;
		const Complex * const w122i = _w122i;

		Complex * const z_copy = new Complex[index(N)];
		for (size_t k = 0; k < index(N); ++k) z_copy[k] = z[k];

		for (size_t lh = 0; lh < n_io / 4 / 2; ++lh)
		{
			backward_out(z_copy, lh, w122i);
		}

		const double sb = _sb, n_io_N = double(n_io) / N;

		for (size_t k = 0; k < N / 2; k += VSIZE / 2)
		{
			const size_t ki = index(2 * k);
			Vc vc = Vc::read(&z_copy[ki]);
			for (size_t i = 0; i < VSIZE / 2; ++i)
			{
				const Complex z1 = vc[2 * i + 0], z2 = vc[2 * i + 1];
				zi[k + i]         = std::llround((z1.real + sb * z2.real) * n_io_N);
				zi[k + i + N / 2] = std::llround((z1.imag + sb * z2.imag) * n_io_N);
			}
		}

		delete[] z_copy;
	}
};

class genefer
{
private:

public:
	void check(const uint32_t b, const size_t n, const char * const exp_residue)
	{
		omp_set_num_threads(3);
		size_t num_threads = 0;
#pragma omp parallel
		{
#pragma omp single
			num_threads = size_t(omp_get_num_threads());
		}
		std::cout << num_threads << " thread(s)." << std::endl;

		const integer exponent(b, n);

		ComplexITransform * t = nullptr;;
		if (n == 1024)      t = new CZIT_CPU_vec_mt<1024, 8>(b, num_threads);
		else if (n == 2048) t = new CZIT_CPU_vec_mt<2048, 8>(b, num_threads);
		else if (n == 4096) t = new CZIT_CPU_vec_mt<4096, 2>(b, num_threads);
		else if (n == 8192) t = new CZIT_CPU_vec_mt<8192, 4>(b, num_threads);
		if (t == nullptr) throw std::runtime_error("exponent is not supported");

		auto t0 = std::chrono::steady_clock::now();

		double err = 0;
		for (int i = int(exponent.bitSize()) - 1; i >= 0; --i)
		{
			const double e = t->squareDup(exponent.bit(size_t(i)));
			err  = std::max(err, e);
		}

		const double time = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

		uint64_t res;
		const bool isPrp = t->isOne(res);
		char residue[30];
		sprintf(residue, "%016" PRIx64, res);

		std::cout << b << "^" << n << " + 1";
		if (isPrp) std::cout << " is prime";
		std::cout << ", err = " << err << ", " << time << " sec, res = " << residue << " [" << exp_residue << "]." << std::endl;
	}
};

int main(/*int argc, char * argv[]*/)
{
	std::cerr << "genefer22: search for Generalized Fermat primes" << std::endl;
	std::cerr << " Copyright (c) 2022, Yves Gallot" << std::endl;
	std::cerr << " genefer22 is free source code, under the MIT license." << std::endl << std::endl;

	try
	{
		genefer g;
		// g.check(399998298, 1024, "");
		// g.check(399998572, 2048, "");
		// g.check(399987078, 4096, "");
		// g.check(399992284, 8192, "");

		g.check(399998300, 1024, "5a82277cc9c6f782");
		g.check(399998574, 2048, "1907ebae0c183e35");
		g.check(399987080, 4096, "dced858499069664");
		g.check(399992286, 8192, "3c918e0f87815627");
	}
	catch (const std::runtime_error & e)
	{
		std::ostringstream ss; ss << std::endl << "error: " << e.what() << ".";
		std::cerr << ss.str() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
