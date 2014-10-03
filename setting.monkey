Strict



Import main



Class Setting
	Field name:String
	Field kind:String
	Field value:Int
	
	Method New( name:String, kind:String, value:Int )
		Self.name = name
		Self.kind = kind
		Self.value = value
	End
	
	Method Copy:Setting()
		Return New Setting( name, kind, value )
	End
End