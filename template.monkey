Strict



Import main



Global templates := New List< Template >()



Function MakeTemplates:Void()
	AddTemplate( "go", 0, 0 )
	AddTemplate( "clear", 0, 0 )
	AddTemplate( "noise", 0, 1 )
	AddSetting( "noise", "density", "f", "9" )
	AddTemplate( "automata", 1, 1 )
	AddTemplate( "conway", 1, 1 )
	AddTemplate( "smooth", 1, 1 )
	AddSetting( "smooth", "laps", "i1-9", "1" )
	AddTemplate( "expand", 1, 1 )
	AddTemplate( "contract", 1, 1 )
	AddTemplate( "darken", 2, 1 )
	AddTemplate( "lighten", 2, 1 )
	AddTemplate( "invert", 1, 1 )
	AddTemplate( "view", 1, 0 )
	AddTemplate( "omni", 0, 0 )
	AddTemplate( "sequence", 1, 1 )
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



Function AddSetting:Void( templateName:String, name:String, kind:String, initial:String )
	Local template:Template = _GetTemplate( templateName )
	'''Local setting:Setting = Setting.Make( name, kind, initial )
	'''template.settings.Insert( name, setting )
End



Class Template
	Field name:String, ins:Int, outs:Int
	'''Field settings:TMap = CreateMap()
	
	Method New( name:String, ins:Int, outs:Int )
		Self.name = name; Self.ins = ins; Self.outs = outs
	End
End