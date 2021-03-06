// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

/// \file BackendRiak.cxx
/// \brief Implementation of the BackendRiak class
/// \author Charis Kouzinopoulos <charalampos.kouzinopoulos@cern.ch>

#include "CCDB/BackendRiak.h"
#include "CCDB/ObjectHandler.h"
#include "request.pb.h"

#include <zlib.h>

#include <FairMQLogger.h>

using namespace o2::ccdb;
using namespace std;

BackendRiak::BackendRiak() = default;

// Compression/decompression code taken from https://panthema.net/2007/0328-ZLibString.html

void BackendRiak::Compress(const std::string& uncompressed_string, std::string& compressed_string)
{
  // z_stream is zlib's control structure
  z_stream zs;
  memset(&zs, 0, sizeof(zs));

  if (deflateInit(&zs, Z_DEFAULT_COMPRESSION) != Z_OK) {
    LOG(ERROR) << "deflateInit failed while compressing";
  }

  zs.next_in = (Bytef*)uncompressed_string.data();
  zs.avail_in = uncompressed_string.size();

  int ret;
  char outbuffer[32768];
  std::string outstring;

  // Get the compressed bytes in blocks of 32768 bytes using repeated calls to deflate
  do {
    zs.next_out = reinterpret_cast<Bytef*>(outbuffer);
    zs.avail_out = sizeof(outbuffer);

    ret = deflate(&zs, Z_FINISH);

    if (outstring.size() < zs.total_out) {
      // append the block to the output string
      outstring.append(outbuffer, zs.total_out - outstring.size());
    }
  } while (ret == Z_OK);

  deflateEnd(&zs);

  if (ret != Z_STREAM_END) {
    LOG(ERROR) << "Exception during zlib compression: (" << ret << ") " << zs.msg;
  }

  compressed_string.assign(outstring);
}

void BackendRiak::Decompress(std::string& uncompressed_string, const std::string& compressed_string)
{
  // z_stream is zlib's control structure
  z_stream zs;
  memset(&zs, 0, sizeof(zs));

  if (inflateInit(&zs) != Z_OK) {
    LOG(ERROR) << "deflateInit failed while decompressing";
  }

  zs.next_in = (Bytef*)compressed_string.data();
  zs.avail_in = compressed_string.size();

  int ret;
  char outbuffer[32768];
  std::string outstring;

  // Get the decompressed bytes in blocks of 32768 bytes using repeated calls to inflate
  do {
    zs.next_out = reinterpret_cast<Bytef*>(outbuffer);
    zs.avail_out = sizeof(outbuffer);

    ret = inflate(&zs, 0);

    if (outstring.size() < zs.total_out) {
      outstring.append(outbuffer, zs.total_out - outstring.size());
    }
  } while (ret == Z_OK);

  inflateEnd(&zs);

  if (ret != Z_STREAM_END) {
    LOG(ERROR) << "Exception during zlib compression: (" << ret << ") " << zs.msg;
  }

  uncompressed_string.assign(outstring);
}

void BackendRiak::Deserialize(const std::string& messageString, std::string& object)
{
  messaging::RequestMessage* requestMessage = new messaging::RequestMessage;
  requestMessage->ParseFromString(messageString);

  object.assign(requestMessage->value());

  delete requestMessage;
}

void BackendRiak::Pack(const std::string& path, const std::string& key, std::string*& messageString)
{
  // Load the AliCDBEntry object from disk
  std::string object;
  ObjectHandler::GetObject(path, object);

  // Compress the object before storing to Riak
  std::string compressed_object;
  Compress(object, compressed_object);

  Serialize(messageString, key, "PUT", "Riak", compressed_object);
}

Condition* BackendRiak::UnPack(std::unique_ptr<FairMQMessage> msg)
{
  // FIXME: how to actually extract a condition or a binary blob here?
  std::string brokerString(static_cast<char*>(msg->GetData()), msg->GetSize());

  // Deserialize the received string
  std::string compressedObject;
  Deserialize(brokerString, compressedObject);

  // Decompress the compressed object
  std::string object;
  Decompress(object, compressedObject);

  // nullptr since no other possibility at moment
  return nullptr;
}
