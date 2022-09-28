#include <exception>
#include <iostream>
#include "ismrmrd/ismrmrd.h"
#include "ismrmrd/dataset.h"
#include "ismrmrd/xml.h"
#include "fftw3.h"
#include "fmt/core.h"

class close_stream : std::exception
{
};

// Helper function for the FFTW library
void circshift(complex_float_t *out, const complex_float_t *in, int xdim, int ydim, int xshift, int yshift)
{
    for (int i = 0; i < ydim; i++)
    {
        int ii = (i + yshift) % ydim;
        for (int j = 0; j < xdim; j++)
        {
            int jj = (j + xshift) % xdim;
            out[ii * xdim + jj] = in[i * xdim + j];
        }
    }
}

#define fftshift(out, in, x, y) circshift(out, in, x, y, (x / 2), (y / 2))

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

void write_image(ISMRMRD::Image<float> &im, std::ostream &output_stream)
{
    write_message_id(output_stream, MRD_IMAGE);
    ISMRMRD::ImageHeader h = im.getHead();
    output_stream.write(reinterpret_cast<char *>(&h), sizeof(ISMRMRD::ImageHeader));
    if (h.attribute_string_len)
    {
        uint64_t attr_length = im.getAttributeStringLength();
        output_stream.write(reinterpret_cast<char *>(&attr_length), sizeof(uint64_t));
        output_stream.write(im.getAttributeString(), h.attribute_string_len);
    }
    output_stream.write(reinterpret_cast<char *>(im.getDataPtr()), im.getDataSize());
}

int main()
{

    auto hdr = read_header(std::cin);

    // Let's print some information from the header
    if (hdr.version)
    {
        std::cerr << "XML Header version: " << hdr.version << std::endl;
    }
    else
    {
        std::cerr << "XML Header unspecified version." << std::endl;
    }

    if (hdr.encoding.size() != 1)
    {
        throw std::runtime_error("This simple reconstruction application only supports one encoding space");
    }

    ISMRMRD::EncodingSpace e_space = hdr.encoding[0].encodedSpace;
    ISMRMRD::EncodingSpace r_space = hdr.encoding[0].reconSpace;

    if (e_space.matrixSize.z != 1)
    {
        throw std::runtime_error("This simple reconstruction application only supports 2D encoding spaces");
    }

    uint16_t nX = e_space.matrixSize.x;
    uint16_t nY = e_space.matrixSize.y;
    uint16_t nCoils = 0;
    ISMRMRD::NDArray<complex_float_t> buffer;
    while (std::cin)
    {
        ISMRMRD::Acquisition acq;
        try
        {
            acq = read_acquisition(std::cin);
        }
        catch (close_stream &)
        {
            break;
        }

        if (!nCoils)
        {
            nCoils = acq.active_channels();

            std::cerr << "Encoding Matrix Size        : [" << e_space.matrixSize.x << ", " << e_space.matrixSize.y << ", " << e_space.matrixSize.z << "]" << std::endl;
            std::cerr << "Reconstruction Matrix Size  : [" << r_space.matrixSize.x << ", " << r_space.matrixSize.y << ", " << r_space.matrixSize.z << "]" << std::endl;
            std::cerr << "Number of Channels          : " << nCoils << std::endl;

            // Allocate a buffer for the data
            std::vector<size_t> dims;
            dims.push_back(nX);
            dims.push_back(nY);
            dims.push_back(nCoils);
            buffer = ISMRMRD::NDArray<complex_float_t>(dims);
            std::fill(buffer.begin(), buffer.end(), complex_float_t(0.0f, 0.0f));
        }

        for (uint16_t c = 0; c < nCoils; c++)
        {
            memcpy(&buffer(0, acq.idx().kspace_encode_step_1, c), &acq.data(0, c), sizeof(complex_float_t) * nX);
        }
    }

    for (uint16_t c = 0; c < nCoils; c++)
    {
        fftwf_complex *tmp = (fftwf_complex *)fftwf_malloc(sizeof(fftwf_complex) * (nX * nY));
        if (!tmp)
        {
            std::cerr << "Error allocating temporary storage for FFTW" << std::endl;
            return -1;
        }
        fftwf_plan p = fftwf_plan_dft_2d(nY, nX, tmp, tmp, FFTW_BACKWARD, FFTW_ESTIMATE);
        fftshift(reinterpret_cast<complex_float_t *>(tmp), &buffer(0, 0, c), nX, nY);
        fftwf_execute(p);
        fftshift(&buffer(0, 0, c), reinterpret_cast<std::complex<float> *>(tmp), nX, nY);
        fftwf_destroy_plan(p);
        fftwf_free(tmp);
    }

    // Allocate an image
    ISMRMRD::Image<float> img_out(r_space.matrixSize.x, r_space.matrixSize.y, 1, 1);
    std::fill(img_out.begin(), img_out.end(), 0.0f);

    // f there is oversampling in the readout direction remove it
    // Take the sqrt of the sum of squares
    uint16_t offset = ((e_space.matrixSize.x - r_space.matrixSize.x) >> 1);
    for (uint16_t y = 0; y < r_space.matrixSize.y; y++)
    {
        for (uint16_t x = 0; x < r_space.matrixSize.x; x++)
        {
            for (uint16_t c = 0; c < nCoils; c++)
            {
                img_out(x, y) += (std::abs(buffer(x + offset, y, c))) * (std::abs(buffer(x + offset, y, c)));
            }
            img_out(x, y) = std::sqrt(img_out(x, y));
        }
    }

    // The following are extra guidance we can put in the image header
    img_out.setImageType(ISMRMRD::ISMRMRD_IMTYPE_MAGNITUDE);
    img_out.setSlice(0);
    img_out.setFieldOfView(r_space.fieldOfView_mm.x, r_space.fieldOfView_mm.y, r_space.fieldOfView_mm.z);

    write_header(hdr, std::cout);
    write_image(img_out, std::cout);
    write_message_id(std::cout, MRD_CLOSE);
    return 0;
}
