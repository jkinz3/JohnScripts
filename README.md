# JohnScripts
A collection of Unreal Engine scripts written by John. JohnScripts.

# How to use
1. Create a "Plugins" folder in your project directory. The project must be a c++ project in order to work.
2. Download or clone into afermentioned "Plugins" folder.
3. Regenerate project files and rebuild project.
4. In the editor, you will find the scripts in the "Build" dropdown.

Right now there is only one, which is a script to generate lightmaps for all selected static meshes. Maybe more someday? I don't know. I haven't eaten dinner yet.

# Limitations
This is designed to be used with the new modeling tools in Unreal, which generate meshes with only one set of UVs. As such, this script 
assumes that there is only one set of UVs and sets the LightMapCoordinateIndex to 1. It will not work on meshes with multiple UV channels from the getgo.
