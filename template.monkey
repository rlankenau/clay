Strict



Import main



Global templates := New List< Template >()



Function MakeTemplates:Void()
	Local tableId:Int = 0
	AddTemplate( "go", 0, 0 )
	AddTemplate( "clear", 0, 0 )
	AddTemplate( "noise", 0, 1 )
	AddSetting( "noise", "density", "f", 9 )
	AddTemplate( "automaton", 1, 1 )
	AddSetting( "automaton", "laps", "i1-9", 1 )
	AddSetting( "automaton", "edge", "dedge", 0 )
	AddSetting( "automaton", "rules", "a9s8", tableId ); tableId += 1
	AddTemplate( "conway", 1, 1 )
	AddTemplate( "smooth", 1, 1 )
	AddSetting( "smooth", "laps", "i1-9", 1 )
	AddSetting( "smooth", "edge", "dedge", 0 )
	AddTemplate( "expand", 1, 1 )
	AddTemplate( "contract", 1, 1 )
	AddTemplate( "shift", 1, 1 )
	AddTemplate( "skew", 1, 1 )
	AddTemplate( "scale", 1, 1 )
	AddTemplate( "darken", 2, 1 )
	AddTemplate( "lighten", 2, 1 )
	AddTemplate( "invert", 1, 1 )
	AddTemplate( "fill", 0, 1 )
	AddSetting( "fill", "color", "b", 1 )
	AddTemplate( "canvas", 0, 1 )
	AddTemplate( "view", 1, 0 )
	AddTemplate( "omni", 0, 0 )
	AddTemplate( "sequence", 1, 1 )
	AddTemplate( "array", 4, 4 )
	AddTemplate( "patch", 4, 4 )
	AddTemplate( "in", 0, 1 )
	AddTemplate( "out", 1, 0 )
	AddTemplate( "parameter", 0, 1 )
End



Function AddTemplate:Void( name:String, ins:Int, outs:Int )
	templates.AddLast( New Template( name, ins, outs ) )
End



Function _GetTemplate:Template( name:String )
	For Local template:Template = EachIn templates
		If template.name = name
			Return template
		EndIf
	Next
	
	Return Null
End



Function AddSetting:Void( templateName:String, name:String, kind:String, initial:Int )
	Local template:Template = _GetTemplate( templateName )
	Local setting:Setting = New Setting( name, kind, initial )
	template.settings.Insert( name, setting )
End



Class Template
	Field name:String, ins:Int, outs:Int
	Field settings := New StringMap< Setting >()
	
	Method New( name:String, ins:Int, outs:Int )
		Self.name = name; Self.ins = ins; Self.outs = outs
	End
End