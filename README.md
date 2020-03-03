# A simple memory class.

To initialize the class properly, you do the following:
```c++
try {
	forceinline::memory_manager memory( "process_name.exe" );
	
	//Do whatever you want to
} catch ( const std::exception& e ) {
	std::cout << e.what( ) << std::endl;
}
```
Everything is commented and should be easily understandable. If not, please add a comment and issue a pull request or create an issue. Thank you.
## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details
