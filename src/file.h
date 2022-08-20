/*
Copyright 2022, Yves Gallot

genefer22 is free source code, under the MIT license (see LICENSE). You can redistribute, use and/or modify it.
Please give feedback to the authors if improvement is realized. It is distributed in the hope that it will be useful.
*/

#pragma once

#include <iomanip>

#include <gmp.h>

#include "pio.h"

class file
{
private:
	FILE * const _cFile;

public:
	file(const std::string & filename, const char * const mode) : _cFile(pio::open(filename.c_str(), mode))
	{
		if (_cFile == nullptr)
		{
			std::ostringstream ss; ss << "cannot open '" << filename << "' file" << std::endl;
			throw std::runtime_error(ss.str());
		}
	}

	file(const std::string & filename) : _cFile(pio::open(filename.c_str(), "rb"))
	{
		// _cFile may be null
	}

	virtual ~file()
	{
		if (_cFile != nullptr) std::fclose(_cFile);
	}

	bool exists() const { return (_cFile != nullptr); }

	void read(char * const ptr, const size_t size)
	{
		const size_t ret = std::fread(ptr , sizeof(char), size, _cFile);
		if (ret == size * sizeof(char)) return;
		std::fclose(_cFile);
		throw std::runtime_error("failure of a read operation");
	}

	void write(const char * const ptr, const size_t size)
	{
		const size_t ret = std::fwrite(ptr , sizeof(char), size, _cFile);
		if (ret == size * sizeof(char)) return;
		std::fclose(_cFile);
		throw std::runtime_error("failure of a write operation");
	}

	void read(mpz_t & z)
	{
		if (mpz_inp_raw(z, _cFile) != 0) return;
		std::fclose(_cFile);
		throw std::runtime_error("failure of a read operation");
	}

	void write(const mpz_t & z)
	{
		if (mpz_out_raw(_cFile, z) != 0) return;
		std::fclose(_cFile);
		throw std::runtime_error("failure of a write operation");
	}

	void print(const char * const str)
	{
		const int ret = std::fprintf(_cFile, str);
		if (ret >= 0) return;
		std::fclose(_cFile);
		throw std::runtime_error("failure of a write operation");
	}
};
