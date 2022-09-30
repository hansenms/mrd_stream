#pragma once

#include <exception>
#include <iostream>
#include <variant>
#include "ismrmrd/ismrmrd.h"
#include "ismrmrd/xml.h"
#include "fmt/core.h"

class close_stream : std::exception
{
};

const unsigned short MRD_HEADER = 3;
const unsigned short MRD_CLOSE = 4;
const unsigned short MRD_ACQUISION = 1008;
const unsigned short MRD_IMAGE = 1022;

unsigned short read_message_id(std::istream &input_stream)
{
    unsigned short id;
    input_stream.read(reinterpret_cast<char *>(&id), sizeof(unsigned short));
    return id;
}

void write_message_id(std::ostream &output_stream, unsigned short id)
{
    output_stream.write(reinterpret_cast<char *>(&id), sizeof(unsigned short));
}

void expect_id(std::istream &input_stream, unsigned short expected_id)
{
    auto id = read_message_id(input_stream);
    if (id == MRD_CLOSE)
    {
        throw close_stream();
    }

    if (id != expected_id)
    {
        throw std::runtime_error(fmt::format("Invalid id {} received, expected {}", id, expected_id));
    }
}

ISMRMRD::IsmrmrdHeader read_header(std::istream &input_stream)
{
    expect_id(input_stream, MRD_HEADER);
    uint32_t hdr_size = 0;
    input_stream.read(reinterpret_cast<char *>(&hdr_size), sizeof(uint32_t));
    ISMRMRD::IsmrmrdHeader hdr;
    if (hdr_size > 0)
    {
        std::vector<char> data(hdr_size);
        input_stream.read(data.data(), hdr_size);
        ISMRMRD::deserialize(std::string(data.data(), data.size()).c_str(), hdr);
    }
    else
    {
        throw std::runtime_error(fmt::format("Expected size > 0, got: {}", hdr_size));
    }

    return hdr;
}

void write_header(ISMRMRD::IsmrmrdHeader &hdr, std::ostream &output_stream)
{
    std::stringstream str;
    ISMRMRD::serialize(hdr, str);
    auto as_str = str.str();
    uint32_t size = as_str.size();
    write_message_id(output_stream, MRD_HEADER);
    output_stream.write(reinterpret_cast<const char *>(&size), sizeof(uint32_t));
    output_stream.write(as_str.c_str(), as_str.size());
}

ISMRMRD::Acquisition read_acquisition(std::istream &input_stream)
{
    expect_id(input_stream, MRD_ACQUISION);
    ISMRMRD::AcquisitionHeader ahead;
    input_stream.read(reinterpret_cast<char *>(&ahead), sizeof(ISMRMRD::AcquisitionHeader));
    ISMRMRD::Acquisition acq;
    acq.setHead(ahead);
    if (ahead.trajectory_dimensions)
    {
        input_stream.read(reinterpret_cast<char *>(acq.getTrajPtr()), ahead.trajectory_dimensions * ahead.number_of_samples * sizeof(float));
    }

    input_stream.read(reinterpret_cast<char *>(acq.getDataPtr()), ahead.number_of_samples * ahead.active_channels * 2 * sizeof(float));
    return acq;
}

void write_acquisition(const ISMRMRD::Acquisition &acq, std::ostream &output_stream)
{
    write_message_id(output_stream, MRD_ACQUISION);
    ISMRMRD::AcquisitionHeader ahead = acq.getHead();
    output_stream.write(reinterpret_cast<const char *>(&ahead), sizeof(ISMRMRD::AcquisitionHeader));
    output_stream.write(reinterpret_cast<const char *>(acq.getTrajPtr()), ahead.trajectory_dimensions * ahead.number_of_samples * sizeof(float));
    output_stream.write(reinterpret_cast<const char *>(acq.getDataPtr()), ahead.number_of_samples * ahead.active_channels * 2 * sizeof(float));
}

typedef std::variant<
    ISMRMRD::Image<unsigned short>,
    ISMRMRD::Image<short>,
    ISMRMRD::Image<unsigned int>,
    ISMRMRD::Image<int>,
    ISMRMRD::Image<float>,
    ISMRMRD::Image<double>,
    ISMRMRD::Image<std::complex<float>>,
    ISMRMRD::Image<std::complex<double>>>
    ISMRMRDImageVariant;

template <typename T>
ISMRMRD::Image<T> construct_image_and_read_pixels(const ISMRMRD::ImageHeader &h, const std::string &attrib_string, std::istream &input_stream)
{
    ISMRMRD::Image<T> im(h.matrix_size[0], h.matrix_size[1], h.matrix_size[2], h.channels);
    im.setAttributeString(attrib_string.c_str());
    input_stream.read(reinterpret_cast<char *>(im.getDataPtr()), im.getDataSize());
    return im;
}

ISMRMRDImageVariant read_image(std::istream &input_stream)
{
    expect_id(input_stream, MRD_IMAGE);
    ISMRMRD::ImageHeader h;
    input_stream.read(reinterpret_cast<char *>(&h), sizeof(ISMRMRD::ImageHeader));
    uint64_t attr_length;
    input_stream.read(reinterpret_cast<char *>(&attr_length), sizeof(uint64_t));
    std::string attrib_string;
    if (attr_length)
    {
        attrib_string.reserve(attr_length);
        input_stream.read(&attrib_string[0], attr_length);
    }

    ISMRMRDImageVariant im;
    if (h.data_type == ISMRMRD::ISMRMRD_USHORT)
    {
        im = construct_image_and_read_pixels<unsigned short>(h, attrib_string, input_stream);
    }
    else if (h.data_type == ISMRMRD::ISMRMRD_SHORT)
    {
        im = construct_image_and_read_pixels<short>(h, attrib_string, input_stream);
    }
    else if (h.data_type == ISMRMRD::ISMRMRD_UINT)
    {
        im = construct_image_and_read_pixels<unsigned int>(h, attrib_string, input_stream);
    }
    else if (h.data_type == ISMRMRD::ISMRMRD_INT)
    {
        im = construct_image_and_read_pixels<int>(h, attrib_string, input_stream);
    }
    else if (h.data_type == ISMRMRD::ISMRMRD_FLOAT)
    {
        im = construct_image_and_read_pixels<float>(h, attrib_string, input_stream);
    }
    else if (h.data_type == ISMRMRD::ISMRMRD_DOUBLE)
    {
        im = construct_image_and_read_pixels<double>(h, attrib_string, input_stream);
    }
    else if (h.data_type == ISMRMRD::ISMRMRD_CXFLOAT)
    {
        im = construct_image_and_read_pixels<std::complex<float>>(h, attrib_string, input_stream);
    }
    else if (h.data_type == ISMRMRD::ISMRMRD_CXDOUBLE)
    {
        im = construct_image_and_read_pixels<std::complex<double>>(h, attrib_string, input_stream);
    }
    else
    {
        throw std::runtime_error("Invalid image data type ... ");
    }
    return im;
}

template <typename T>
void write_image(ISMRMRD::Image<T> &im, std::ostream &output_stream)
{
    write_message_id(output_stream, MRD_IMAGE);
    ISMRMRD::ImageHeader h = im.getHead();
    output_stream.write(reinterpret_cast<char *>(&h), sizeof(ISMRMRD::ImageHeader));
    uint64_t attr_length = im.getAttributeStringLength();
    output_stream.write(reinterpret_cast<char *>(&attr_length), sizeof(uint64_t));
    if (attr_length)
    {
        output_stream.write(im.getAttributeString(), h.attribute_string_len);
    }
    output_stream.write(reinterpret_cast<char *>(im.getDataPtr()), im.getDataSize());
}
