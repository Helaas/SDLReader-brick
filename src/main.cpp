#include "app.h"     
#include <SDL.h>    
#include <SDL_ttf.h> 
#include <iostream>  

// --- Main Function ---
int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " <document_file.pdf/.djvu>" << std::endl;
        return 1;
    }

    try
    {
        App app(argv[1], 800, 600);
        app.run();
    }
    catch (const std::runtime_error &e)
    {
        std::cerr << "Application Error: " << e.what() << std::endl;
        return 1; 
    }
    catch (const std::exception &e)
    {
        std::cerr << "An unexpected error occurred: " << e.what() << std::endl;
        return 1; 
    }

    TTF_Quit();
    SDL_Quit();

    return 0; 
}
