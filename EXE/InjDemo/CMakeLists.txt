add_executable(InjDemo WIN32
	Enum.c
	InjDemo.c
	List.c
	InjLib.rc
)
target_link_libraries(InjDemo comctl32)
set_target_properties(InjDemo
	PROPERTIES
		WIN32_EXECUTABLE TRUE
)

# create a command that will copy this file
add_custom_command(
	OUTPUT InjLib.dll
	COMMENT "Copying InjLib"
	COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:InjLib> InjLib.dll
)

# create a target that will run the copy command if the destination file is out of date
add_custom_target(
	copy_injlib
	DEPENDS InjLib.dll
)

add_dependencies(copy_injlib InjLib)
add_dependencies(InjDemo copy_injlib)