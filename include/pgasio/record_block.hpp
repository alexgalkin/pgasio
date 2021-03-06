/**
    Copyright 2017-20187, Kirit Sælensminde. <https://kirit.com/pgasio/>
*/


#pragma once


#include <pgasio/memory.hpp>
#include <pgasio/network.hpp>

#include <boost/asio/spawn.hpp>


namespace pgasio {


    /// Store a block of data rows. The data buffer is non-aligned.
    class record_block {
        std::size_t m_columns;
        std::vector<byte_view> m_fields;
        unaligned_slab m_buffer;

    public:
        /// An empty block shows that there are no more to come
        record_block()
        : m_columns{} {
        }

        /// The block is intiialised to hold a number of bytes of data row
        /// network messages and indexes the columns into those through
        /// the records member;
        explicit record_block(std::size_t column_count,
            std::size_t record_size = 512,  // Mean record size
            std::size_t bytes = 4u << 20) // MB of record data
        : m_columns(column_count), m_buffer(bytes) {
            const auto expected_records = (bytes + record_size - 1) / record_size;
            m_fields.reserve(m_columns * expected_records);
        }

        /// Not copyable
        record_block(const record_block &) = delete;
        record_block &operator = (const record_block &) = delete;
        /// Moveable
        record_block(record_block &&) = default;
        record_block &operator = (record_block &&) = default;

        /// Allow conversion to a bool context. Returns true if the block
        /// contains data
        operator bool () const {
            return m_columns > 0;
        }

        /// The number of bytes for row data still available in this block
        std::size_t remaining() const {
            return m_buffer.remaining();
        }
        /// The number of bytes that have been used in the block
        std::size_t used_bytes() const {
            return m_buffer.allocated();
        }

        /// Read the next data message into the block
        template<typename S, typename Y>
        void read_data_row(S &socket, std::size_t bytes, Y yield) {
            assert(bytes <= remaining());
            auto message_data = m_buffer.allocate(bytes);
            transfer(socket, message_data, bytes, yield);
            decoder message(message_data);
            message.read_int16();
            while ( message.remaining() ) {
                const auto bytes = message.read_int32();
                if ( bytes == -1 ) {
                    m_fields.push_back(byte_view());
                } else {
                    m_fields.push_back(message.read_bytes(bytes));
                }
            }
            assert(m_fields.size() % m_columns == 0);
        }

        /// Fill the block with data. Return zero if there is no more data to come
        template<typename S, typename Y>
        std::size_t read_rows(S &socket, std::size_t bytes, Y yield) {
            do {
                read_data_row(socket, bytes, yield);
                auto next = message_header(socket, yield);
                if ( next.type == 'D' ) {
                    bytes = next.body_size;
                } else if ( next.type == 'C' ) {
                    next.message_body(socket, yield);
                    return 0;
                } else {
                    throw std::logic_error(std::string("Unknown message type: ") + next.type);
                }
            } while ( bytes <= remaining() );
            return bytes;
        }

        /// Return the current record fields
        array_view<const byte_view> fields() const {
            return m_fields;
        }
    };


}

