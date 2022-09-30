import ctypes
from logging import exception
import sys
import struct
from typing import BinaryIO
import numpy as np
import ismrmrd
import warnings

with warnings.catch_warnings():
    # ignore warnings about pyfftw using distutils Version classes
    warnings.filterwarnings("ignore", module="pyfftw", category=DeprecationWarning)
    from pyfftw.interfaces.numpy_fft import fftshift, ifftshift, fftn, ifftn
    from pyfftw.interfaces.cache import enable as enable_cache

# Enable pyFFTW caching
enable_cache()

MRD_HEADER = 3
MRD_CLOSE = 4
MRD_ACQUISION = 1008
MRD_IMAGE = 1022

class CloseStreamException(Exception):
    pass

def read_message_id(instream: BinaryIO):
    return struct.unpack('H',instream.read(2))[0]

def read_and_expect_id(instream: BinaryIO, expected: int):
    id = read_message_id(instream)
    if id == MRD_CLOSE:
        raise CloseStreamException
    if id != expected:
        raise Exception(f"Unexpected message id {id}, expected {expected}")

def write_message_id(outstream: BinaryIO, msg_id: int):
    outstream.write(struct.pack('H',msg_id))

def read_header(instream: BinaryIO) -> ismrmrd.xsd.ismrmrdHeader:
    read_and_expect_id(instream, MRD_HEADER)
    hdr_size = struct.unpack('I',instream.read(4))[0]
    doc = instream.read(hdr_size)
    return ismrmrd.xsd.CreateFromDocument(doc)

def write_header(outstream: BinaryIO, header: ismrmrd.xsd.ismrmrdHeader):
    xml_bytes = bytes(ismrmrd.xsd.ToXML(header),'ascii')
    write_message_id(outstream, MRD_HEADER)
    outstream.write(struct.pack('I',len(xml_bytes)))
    outstream.write(xml_bytes)

def read_acquisition(instream: BinaryIO):
    read_and_expect_id(instream, MRD_ACQUISION)
    return ismrmrd.Acquisition.deserialize_from(instream.read)


def write_image(outstream: BinaryIO, img: ismrmrd.Image):
    write_message_id(outstream, MRD_IMAGE)
    img.serialize_into(outstream.write)

def k2i(k: np.ndarray, dim=None, img_shape=None) -> np.ndarray:
    if not dim:
        dim = range(k.ndim)
    img = fftshift(ifftn(ifftshift(k, axes=dim), s=img_shape, axes=dim), axes=dim)
    img *= np.sqrt(np.prod(np.take(img.shape, dim)))
    return img


def recon(kspace: np.ndarray):
    if kspace.shape[-3] > 1:
        img = k2i(kspace, dim=[-1, -2, -3])
    else:
        img = k2i(kspace, dim=[-1, -2])

    for contrast in range(img.shape[0]):
        for islice in range(img.shape[1]):
            img_combined = np.sqrt(np.abs(np.sum(img[contrast, islice] * np.conj(img[contrast, islice]), axis=0)).astype('float32'))
            yield img_combined

def crop_image(image_bytes: np.ndarray, rNx: int, rNy: int, rNz: int):
    image_bytes = image_bytes.squeeze()

    xoffset = int((image_bytes.shape[0] + 1)/2) - int((rNx+1)/2)
    yoffset = int((image_bytes.shape[1] + 1)/2) - int((rNy+1)/2)
    if len(image_bytes.shape) == 3:
        zoffset = int((image_bytes.shape[2] + 1)/2) - int((rNz+1)/2)
        image_bytes = image_bytes[xoffset:(xoffset+rNx), yoffset:(yoffset+rNy), zoffset:(zoffset+rNz)]
    elif len(image_bytes.shape) == 2:
        image_bytes = image_bytes[xoffset:(xoffset+rNx), yoffset:(yoffset+rNy)]
    else:
        raise Exception('Array img_bytes should have 2 or 3 dimensions')
    return image_bytes

def reconstruction(instream: BinaryIO, outstream: BinaryIO):
    header = read_header(instream)
    enc = header.encoding[0]

    # Matrix size
    if enc.encodedSpace and enc.reconSpace and enc.encodedSpace.matrixSize and enc.reconSpace.matrixSize:
        eNx = enc.encodedSpace.matrixSize.x
        eNy = enc.encodedSpace.matrixSize.y
        eNz = enc.encodedSpace.matrixSize.z
        rNx = enc.reconSpace.matrixSize.x
        rNy = enc.reconSpace.matrixSize.y
        rNz = enc.reconSpace.matrixSize.z
    else:
        raise Exception('Required encoding information not found in header')

    # Number of Slices, Reps, Contrasts, etc.
    ncoils = 1
    if header.acquisitionSystemInformation and header.acquisitionSystemInformation.receiverChannels:
        ncoils = header.acquisitionSystemInformation.receiverChannels

    if enc.encodingLimits and enc.encodingLimits.slice != None:
        nslices = enc.encodingLimits.slice.maximum + 1
    else:
        nslices = 1

    ncontrasts = 1
    if enc.encodingLimits and enc.encodingLimits.contrast != None:
        ncontrasts = enc.encodingLimits.contrast.maximum + 1

    if enc.encodingLimits and enc.encodingLimits.kspace_encoding_step_1 != None:
        ky_offset = int((eNy+1)/2) - enc.encodingLimits.kspace_encoding_step_1.center
    else:
        ky_offset = 0

    current_rep = -1
    buffer = None
    ref_acq = None

    def push_images(buffer: np.ndarray, ref_acq=ismrmrd.Acquisition):
        for rec in recon(buffer):
            img_bytes = crop_image(rec, rNx, rNy, rNz)
            img = ismrmrd.Image.from_array(img_bytes, acquisition=ref_acq, transpose=False)
            write_image(outstream, img)

    write_header(outstream, header)
    while instream.readable():
        try:
            acq = read_acquisition(instream)
        except CloseStreamException:
            break

        if acq.idx.repetition != current_rep:
            if buffer is not None:
                push_images(buffer, ref_acq)

            # Reset buffer
            if acq.data.shape[-1] == eNx:
                readout_length = eNx
            else:
                readout_length = rNx  # Readout oversampling has probably been removed upstream

            buffer = np.zeros((ncontrasts, nslices, ncoils, eNz, eNy, readout_length), dtype=np.complex64)
            current_rep = acq.idx.repetition
            ref_acq = acq

        # Stuff into the buffer
        if buffer is not None :
            buffer[acq.idx.contrast, acq.idx.slice, :, acq.idx.kspace_encode_step_2, acq.idx.kspace_encode_step_1 + ky_offset, :] = acq.data

    if buffer is not None:
        push_images(buffer, ref_acq)
        buffer = None

    write_message_id(outstream, MRD_CLOSE)

if __name__ == "__main__":
    reconstruction(sys.stdin.buffer, sys.stdout.buffer)
