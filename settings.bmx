Type Setting
	Field name:String
	Field kind:String
	Field value:String
	
	Function Make:Setting( name:String, kind:String, value:String )
		Local setting:Setting = New Setting
		setting.name = name
		setting.kind = kind
		setting.value = value
		Return setting
	EndFunction
	
	Method Copy:Setting()
		Return Make( name, kind, value )
	EndMethod
EndType
