#include "memory_manager.h"

#include <iostream>
#include <vector>

namespace forceinline {
	memory_manager::memory_manager( std::string_view process ) {
		if ( process.empty( ) )
			throw std::invalid_argument( "memory_manager::memory_manager: process argument is empty" );

		attach_to_process( process );
	}

	memory_manager::~memory_manager( ) {
		if ( m_proc_handle )
			CloseHandle( m_proc_handle );
	}

	MODULEENTRY32 memory_manager::get_module_entry( std::string_view module ) {
		// Create snapshot that includes 32 and 64 bit modules
		HANDLE snapshot = CreateToolhelp32Snapshot( TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, m_proc_id );
		
		// Is the snapshot valid?
		if ( snapshot == INVALID_HANDLE_VALUE )
			throw std::exception( "memory_manager::get_module_entry: failed to create snapshot" );

		// Iterate through the processes modules
		MODULEENTRY32 me;
		me.dwSize = sizeof me;

		if ( Module32First( snapshot, &me ) ) {
			do {
				// We found our module, stop iterating
				if ( module == me.szModule ) {
					// Remember to close handle
					CloseHandle( snapshot );
					return me;
				}
			} while ( Module32Next( snapshot, &me ) );
		}

		// We didn't find the module. Remember to close handle!
		CloseHandle( snapshot );
		throw std::runtime_error( "memory_manager::get_module_entry: module " + std::string( module ) + " not found" );
	}

	std::uintptr_t memory_manager::get_module_base( std::string_view module ) {
		return std::uintptr_t( get_module_entry( module ).modBaseAddr );
	}

	// Wrapper for find_pattern below
	std::uintptr_t memory_manager::find_pattern( std::string_view module, std::string_view pattern ) {
		// Get the target modules info
		auto module_entry = get_module_entry( module );

		// Get module begin and size
		std::size_t module_size = std::size_t( module_entry.modBaseSize );
		std::uintptr_t module_begin = std::uintptr_t( module_entry.modBaseAddr );
		
		// Scan for our pattern
		return find_pattern( module_begin, module_size, pattern );
	}

	/*
		Find pattern in given module
		Example pattern: "E8 ?? ?? ?? ?? CC"
		Pattern can NOT be without spaces. Wildcars can be single or double question marks, but they have to be seperated with spaces.
	*/
	std::uintptr_t memory_manager::find_pattern( std::uintptr_t module_begin, std::size_t module_size, std::string_view pattern ) {
		// Allocate sizeof module bytes
		std::vector< std::uint8_t > module_bytes( module_size );

		constexpr std::size_t page_size = 4096;
		std::size_t num_pages = module_size / page_size;
		std::size_t page_remainder = module_size % page_size;

		// Function to read a page
		std::uintptr_t total_bytes_read = 0x0;
		auto read_page = [ & ]( std::uintptr_t start, std::size_t size ) -> void {
			// Modify the page so we can read it, also add execute and write rights so we don't crash our process
			DWORD old_protect;
			VirtualProtectEx( m_proc_handle, reinterpret_cast< void* >( start ), size, PAGE_EXECUTE_READWRITE, &old_protect );

			// Read a whole page (or the remainder of it)
			std::size_t bytes_read;
			read_ex< std::uint8_t >( module_bytes.data( ) + total_bytes_read, start, size, &bytes_read );
			
			// Add the read bytes
			total_bytes_read += bytes_read;
		
			// Restore the old protection flags
			VirtualProtectEx( m_proc_handle, reinterpret_cast< void* >( start ), size, old_protect, &old_protect );

			// Did we read as much as we requested to?
			if ( bytes_read != size )
				throw std::runtime_error( "memory_manager::find_pattern: failed to read page" );
		};

		// Read each page seperately
		for ( std::size_t i = 0; i < num_pages; i++ )
			read_page( module_begin + i * page_size, page_size );

		// Read remainder of page
		read_page( module_begin + --num_pages * page_size, page_remainder );

		// We didn't read the whole module
		if ( total_bytes_read != module_size )
			throw std::runtime_error( "memory_manager::find_pattern: failed to read whole module" );

		auto get_byte_vector_and_mask = [ ]( const std::string& pattern, std::vector< std::uint8_t >& byte_vec, std::string& mask ) {
			for ( std::size_t i = 0; i < pattern.length( ); i++ ) {
				// Skip spaces
				if ( pattern[ i ] == ' ' )
					continue;

				// Add wildcard
				if ( pattern[ i ] == '?' ) {
					while ( pattern[ i + 1 ] == '?' )
						i++;

					mask.append( "?" );
					byte_vec.push_back( 0x0 );
					continue;
				}

				// Add converted byte
				byte_vec.push_back( std::stoi( pattern.substr( i++, 2 ), nullptr, 16 ) );
				mask.append( "x" );
			}
		};
		
		// Generate pattern bytes and mask from signature
		std::string pattern_mask;
		std::vector< std::uint8_t > pattern_bytes;
		get_byte_vector_and_mask( pattern.data( ), pattern_bytes, pattern_mask );

		// Scan for our pattern
		std::uintptr_t pattern_offset = 0x0;
		for ( std::size_t i = 0; i < module_size; i++ ) {
			// Found a matching byte
			if ( module_bytes[ i ] == pattern_bytes[ 0 ] ) {
				// Save offset
				pattern_offset = i;

				// Check the pattern further
				for ( std::size_t j = 1; j < pattern_bytes.size( ); j++ ) {
					auto pattern_byte = pattern_bytes[ j ];
					auto mask_byte = pattern_mask[ j ];

					// Is current mask byte a wildcard?
					if ( mask_byte != '?' ) {
						// Bytes don't match and pattern at current position, start over
						if ( pattern_byte != module_bytes[ i + j ] )
							break;
					}

					// Pattern matches, return offset
					if ( j + 1 == pattern_bytes.size( ) )
						return module_begin + pattern_offset;
				}

				// Didn't find pattern, reset our pattern offset
				pattern_offset = 0;
			}
		}

		throw std::runtime_error( "memory_manager::find_pattern: pattern '" + std::string( pattern ) + "' not found" );
	}

	std::uintptr_t memory_manager::operator[ ]( std::string_view module ) {
		return get_module_base( module );
	}

	void memory_manager::attach_to_process( std::string_view process ) {
		// Create snapshot
		HANDLE snapshot = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, NULL );

		// Is the snapshot valid?
		if ( snapshot == INVALID_HANDLE_VALUE )
			throw std::exception( "memory_manager::attach_to_process: failed to create snapshot" );


		// Enumerate running processes
		PROCESSENTRY32 pe;
		pe.dwSize = sizeof pe;
		
		if ( Process32First( snapshot, &pe ) ) {
			do {
				// Have we found our target process?
				if ( process.compare( pe.szExeFile ) == 0 ) {
					// Open handle with rights to read, write, allocate memory and create remote threads
					m_proc_id = pe.th32ProcessID;
					m_proc_handle = OpenProcess( PROCESS_ALL_ACCESS, FALSE, m_proc_id );
					break;
				}
			} while ( Process32Next( snapshot, &pe ) );
		}

		// Close snapshot handle
		CloseHandle( snapshot );
		
		if ( !m_proc_handle )
			throw std::exception( "memory_manager::attach_to_process: process not found" );
	}
}