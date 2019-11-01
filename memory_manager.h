#pragma once
#include <string>

#include <Windows.h>
#include <Psapi.h>
#include <TlHelp32.h>
#include <stdexcept>

namespace forceinline {
	class memory_manager {
	public:
		memory_manager( std::string_view process );
		~memory_manager( );

		memory_manager( const memory_manager& ) noexcept = delete;
		memory_manager& operator=( const memory_manager& ) noexcept = delete;

		MODULEENTRY32 get_module_entry( std::string_view module );
		std::uintptr_t get_module_base( std::string_view module );

		std::uintptr_t find_pattern( std::string_view module, std::string_view pattern );
		std::uintptr_t find_pattern( std::uintptr_t module_begin, std::size_t module_size, std::string_view pattern );

		template < typename T >
		T read( std::uintptr_t address, std::size_t* bytes_read = nullptr ) {
			T temp_val;		
			
			if ( !!ReadProcessMemory( m_proc_handle, reinterpret_cast< const void* >( address ), &temp_val, sizeof T, reinterpret_cast< SIZE_T* >( bytes_read ) ) )
				return temp_val;

			throw std::runtime_error( "memory_manager::read: ReadProcessMemory failure" );
		}

		/*
			Reads custom length.
			Example: you want to read a float[ 64 ].
			you'd use this as follows:
			read_ex< float >( float_arr_ptr, 0xDEADBEEF, 64 )
		*/
		template < typename T >
		void read_ex( T* out_object_ptr, std::uintptr_t address, std::size_t object_count, std::size_t* bytes_read = nullptr ) {
			if ( !ReadProcessMemory( m_proc_handle, reinterpret_cast< const void* >( address ), out_object_ptr, sizeof T * object_count, reinterpret_cast< SIZE_T* >( bytes_read ) ) )
				throw std::runtime_error( "memory_manager::read_ex: ReadProcessMemory failure" );
		}

		template < typename T >
		void write( std::uintptr_t address, T value, std::size_t* bytes_written = nullptr ) {
			if ( !WriteProcessMemory( m_proc_handle, reinterpret_cast< void* >( address ), &value, sizeof T, reinterpret_cast< SIZE_T* >( bytes_written ) ) )
				throw std::runtime_error( "memory_manager::write: WriteProcessMemory failure" );
		}

		//See read_ex
		template < typename T >
		void write_ex( T* object_ptr, std::uintptr_t address, std::size_t object_count, std::size_t* bytes_written = nullptr ) {
			if ( !WriteProcessMemory( m_proc_handle, reinterpret_cast< void* >( address ), object_ptr, sizeof T * object_count, reinterpret_cast< SIZE_T* >( bytes_written ) ) )
				throw std::runtime_error( "memory_manager::write_ex: WriteProcessMemory failure" );
		}

		// Returns the module base address
		std::uintptr_t operator[ ]( std::string_view module );
		
	// Internals
	private:
		void attach_to_process( std::string_view process );

		HANDLE m_proc_handle = nullptr;
		std::uintptr_t m_proc_id = 0;
	};
}